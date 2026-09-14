// main/main.c —— FoloOS 启动器:初始化公共硬件、应用菜单与按键分发。
// 按键语义:上/下短按只移动选择,OK 只确认;应用内长按上键返回主页。
#include <stdbool.h>
#include <stddef.h>

#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_pins.h"      // 错误日志里要打印 BSP_LCD_* 引脚号
#include "app.h"
#include "coding_bridge.h"
#include "ui_icons.h"
#include "ui_pixel.h"
#include "ui_pixel_math.h"
#include "ui_sfx.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

static const char *TAG = "main";

enum {
    APP_CODING_INDEX = 0,
    APP_POMODORO_INDEX,
    APP_WORD_BEAR_INDEX,
    APP_SETTINGS_INDEX,
};

static const app_entry_t APPS[] = {
    { "编程伴侣", &ui_icon_coding,    app_coding_enter,   app_coding_exit,   app_coding_key   },
    { "番茄专注", &ui_icon_pomodoro,  app_pomodoro_enter, app_pomodoro_exit, app_pomodoro_key },
    { "单词熊",   &ui_icon_word_bear, app_word_bear_enter, app_word_bear_exit, app_word_bear_key },
    { "系统设置", &ui_icon_settings,  app_settings_enter, app_settings_exit, app_settings_key },
};
#define APP_COUNT (sizeof(APPS) / sizeof(APPS[0]))
#define VISIBLE_APP_COUNT 3

static lv_obj_t *s_menu_scr;
static lv_obj_t *s_cards[VISIBLE_APP_COUNT];
static lv_obj_t *s_rows[VISIBLE_APP_COUNT];
static lv_obj_t *s_icons[VISIBLE_APP_COUNT];
static lv_obj_t *s_arrows[VISIBLE_APP_COUNT];
static lv_obj_t *s_scroll_thumb;
static lv_timer_t *s_menu_anim;
static lv_timer_t *s_power_timer;
static int  s_sel;                 // 当前选中项
static int  s_first_visible;
static unsigned s_anim_frame;
static int  s_active = -1;         // 当前应用;-1 = 在菜单
static bool s_home_requested;
static bool s_power_keep_awake;
static uint32_t s_last_activity_ms;
static ui_power_state_t s_power_state = UI_POWER_ACTIVE;

static void power_visuals_active(bool active)
{
    if (s_menu_anim != NULL) {
        if (active) lv_timer_resume(s_menu_anim);
        else lv_timer_pause(s_menu_anim);
    }
    if (s_active == APP_CODING_INDEX) app_coding_display_active(active);
}

static void power_apply(ui_power_state_t state)
{
    if (state == s_power_state) return;
    s_power_state = state;
    if (state == UI_POWER_ACTIVE) {
        bsp_display_enable(true);
        bsp_display_backlight(app_settings_brightness());
        power_visuals_active(true);
    } else if (state == UI_POWER_DIMMED) {
        bsp_display_backlight(
            ui_power_dim_backlight(app_settings_brightness()));
        power_visuals_active(false);
    } else {
        bsp_display_backlight(0);
        bsp_display_enable(false);
        power_visuals_active(false);
    }
}

void app_power_activity(void)
{
    s_last_activity_ms = lv_tick_get();
    power_apply(UI_POWER_ACTIVE);
}

void app_power_hold_awake(bool keep_awake)
{
    bool changed = s_power_keep_awake != keep_awake;
    s_power_keep_awake = keep_awake;
    if (changed) app_power_activity();
}

static void power_tick(lv_timer_t *timer)
{
    (void)timer;
    uint32_t idle_ms = lv_tick_get() - s_last_activity_ms;
    power_apply(ui_power_state_for_idle(idle_ms, s_power_keep_awake));
}

void app_request_home(void) {
    s_home_requested = true;
}

static void ensure_selection_visible(void)
{
    if (s_sel < s_first_visible) {
        s_first_visible = s_sel;
    } else if (s_sel >= s_first_visible + VISIBLE_APP_COUNT) {
        s_first_visible = s_sel - VISIBLE_APP_COUNT + 1;
    }
}

static void menu_refresh(void) {
    ensure_selection_visible();
    for (int slot = 0; slot < VISIBLE_APP_COUNT; ++slot) {
        int app_index = s_first_visible + slot;
        bool visible = app_index < (int)APP_COUNT;
        if (!visible) {
            lv_obj_add_flag(s_cards[slot], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_cards[slot], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_rows[slot], APPS[app_index].name);
        lv_image_set_src(s_icons[slot], APPS[app_index].icon);
        bool selected = app_index == s_sel;
        lv_obj_set_style_bg_opa(s_cards[slot], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(s_cards[slot], lv_color_hex(UI_ACCENT), 0);
        lv_obj_set_style_border_width(s_cards[slot], selected ? 2 : 0, 0);
        lv_obj_set_style_text_color(s_rows[slot], lv_color_hex(UI_WHITE), 0);
        if (selected) lv_obj_clear_flag(s_arrows[slot], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_arrows[slot], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_icons[slot], 9, 5);
    }

    if (s_scroll_thumb != NULL) {
        int track_h = 234;
        int thumb_h = track_h * VISIBLE_APP_COUNT / (int)APP_COUNT;
        if (thumb_h < 24) thumb_h = 24;
        int max_first = (int)APP_COUNT - VISIBLE_APP_COUNT;
        int travel = track_h - thumb_h;
        int thumb_y = max_first > 0 ? travel * s_first_visible / max_first : 0;
        lv_obj_set_y(s_scroll_thumb, 47 + thumb_y);
        lv_obj_set_height(s_scroll_thumb, thumb_h);
    }
}

static void menu_animate(lv_timer_t *timer)
{
    (void)timer;
    s_anim_frame = (s_anim_frame + 1) % 10;
    for (int slot = 0; slot < VISIBLE_APP_COUNT; ++slot) {
        int app_index = s_first_visible + slot;
        int jump = app_index == s_sel ? ui_pixel_jump_offset(s_anim_frame) : 0;
        lv_obj_set_y(s_icons[slot], 5 + jump);
    }
}

static void menu_destroy(void)
{
    if (s_menu_anim != NULL) {
        lv_timer_delete(s_menu_anim);
        s_menu_anim = NULL;
    }
    if (s_menu_scr != NULL) {
        lv_obj_delete(s_menu_scr);
        s_menu_scr = NULL;
    }
    s_scroll_thumb = NULL;
}

static void menu_build(void) {
    s_menu_scr = ui_pixel_menu_screen_create("智能工作台");

    for (int slot = 0; slot < VISIBLE_APP_COUNT; ++slot) {
        int y = 46 + slot * 84;
        s_cards[slot] = lv_obj_create(s_menu_scr);
        lv_obj_remove_flag(s_cards[slot], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_cards[slot], 8, y);
        lv_obj_set_size(s_cards[slot], 210, 77);
        lv_obj_set_style_pad_all(s_cards[slot], 0, 0);
        lv_obj_set_style_radius(s_cards[slot], 0, 0);
        lv_obj_set_style_bg_opa(s_cards[slot], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_cards[slot], 0, 0);
        s_icons[slot] = lv_image_create(s_cards[slot]);
        lv_obj_set_size(s_icons[slot], 64, 64);
        lv_obj_set_pos(s_icons[slot], 9, 5);
        s_rows[slot] = lv_label_create(s_cards[slot]);
        lv_obj_set_style_text_font(s_rows[slot], ui_menu_font(), 0);
        lv_obj_set_style_text_align(s_rows[slot], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_width(s_rows[slot], 100);
        lv_obj_align(s_rows[slot], LV_ALIGN_LEFT_MID, 92, 0);
        s_arrows[slot] = ui_pixel_label(s_cards[slot], "▶", ui_menu_font(), UI_ACCENT);
        lv_obj_align(s_arrows[slot], LV_ALIGN_RIGHT_MID, -7, 0);
    }

    if (APP_COUNT > VISIBLE_APP_COUNT) {
        lv_obj_t *track = lv_obj_create(s_menu_scr);
        lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(track, 230, 47);
        lv_obj_set_size(track, 4, 234);
        lv_obj_set_style_pad_all(track, 0, 0);
        lv_obj_set_style_border_width(track, 0, 0);
        lv_obj_set_style_radius(track, 0, 0);
        lv_obj_set_style_bg_color(track, lv_color_hex(UI_BORDER), 0);
        s_scroll_thumb = lv_obj_create(s_menu_scr);
        lv_obj_remove_flag(s_scroll_thumb, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_scroll_thumb, 230, 47);
        lv_obj_set_width(s_scroll_thumb, 4);
        lv_obj_set_style_pad_all(s_scroll_thumb, 0, 0);
        lv_obj_set_style_border_width(s_scroll_thumb, 0, 0);
        lv_obj_set_style_radius(s_scroll_thumb, 0, 0);
        lv_obj_set_style_bg_color(s_scroll_thumb, lv_color_hex(UI_ACCENT), 0);
    }

    lv_obj_t *hint_select = ui_pixel_label(s_menu_scr, "上下选择",
                                           ui_cn_font(), UI_PRIMARY);
    lv_obj_set_pos(hint_select, 60, 296);
    lv_obj_t *hint_ok = ui_pixel_label(s_menu_scr, "OK确认",
                                       ui_cn_font(), UI_WHITE);
    lv_obj_set_pos(hint_ok, 132, 296);

    s_anim_frame = 0;
    menu_refresh();
    s_menu_anim = lv_timer_create(menu_animate, 90, NULL);
    lv_screen_load(s_menu_scr);
}

static void enter_menu(void) {
    s_active = -1;
    s_home_requested = false;
    menu_build();
}

// 按键回调运行在 button 组件的任务里,操作 LVGL 必须加锁。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!bsp_lvgl_lock(500)) return;

    if (ev == BSP_BTN_CLICK || ev == BSP_BTN_LONG) {
        bool display_was_off = s_power_state == UI_POWER_DISPLAY_OFF;
        app_power_activity();
        if (display_was_off) {
            bsp_lvgl_unlock();
            return;
        }
    }

    if (s_active >= 0) {
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_UP) {
            ui_sfx_play(UI_SFX_CONFIRM);
            APPS[s_active].exit();
            enter_menu();
            bsp_lvgl_unlock();
            return;
        }
        if (ev == BSP_BTN_CLICK && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
            ui_sfx_play(UI_SFX_MOVE);
        } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK &&
                   !(s_active == APP_CODING_INDEX && app_coding_ok_is_record_action()) &&
                   s_active != APP_WORD_BEAR_INDEX) {
            ui_sfx_play(UI_SFX_CONFIRM);
        }
        APPS[s_active].key(btn, ev);
        if (s_home_requested) {
            s_home_requested = false;
            APPS[s_active].exit();
            enter_menu();
        }
    } else if (ev == BSP_BTN_CLICK) {
        if (btn == BSP_BTN_UP)   { ui_sfx_play(UI_SFX_MOVE); s_sel = (s_sel + APP_COUNT - 1) % APP_COUNT; menu_refresh(); }
        if (btn == BSP_BTN_DOWN) { ui_sfx_play(UI_SFX_MOVE); s_sel = (s_sel + 1) % APP_COUNT;             menu_refresh(); }
        if (btn == BSP_BTN_OK) {
            ui_sfx_play(UI_SFX_CONFIRM);
            s_active = s_sel;
            menu_destroy();
            APPS[s_active].enter();
        }
    }
    bsp_lvgl_unlock();
}

void app_main(void) {
    ESP_LOGI(TAG, "FoloOS system shell starting");

    bsp_i2c_init();
    bsp_i2c_scan();

    // 屏幕是系统 UI 载体,失败就没有菜单可言 —— 打清楚日志后退出,
    // 不做"串口菜单"降级(那会让本文件复杂一倍,违背参考示例的初衷)。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,FoloOS 无法继续。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bool button_ok = bsp_button_init(on_key, NULL) == ESP_OK;
    bool audio_ok = bsp_audio_init() == ESP_OK;
    bool battery_ok = bsp_battery_init() == ESP_OK;
    app_coding_set_audio_available(audio_ok);
    ui_sfx_init(audio_ok);
    ui_sfx_set_volume(app_settings_sfx_volume());

    if (bsp_lvgl_lock(1000)) {
        enter_menu();
        lv_refr_now(NULL);
        s_last_activity_ms = lv_tick_get();
        bsp_display_backlight(app_settings_brightness());
        s_power_timer = lv_timer_create(power_tick, 1000, NULL);
        bsp_lvgl_unlock();
    }

    bool bridge_ok = coding_bridge_init() == ESP_OK;

    if (button_ok && bridge_ok) {
        esp_err_t valid = esp_ota_mark_app_valid_cancel_rollback();
        if (valid != ESP_OK && valid != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "OTA image validation state: %s", esp_err_to_name(valid));
        }
    }

    ESP_LOGI(TAG, "ready: Display=1 Button=%d Audio=%d Battery=%d Bridge=%d",
             button_ok, audio_ok, battery_ok, bridge_ok);
}
