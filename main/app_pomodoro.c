#include "app.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_timer.h"
#include "lvgl.h"
#include "nvs.h"
#include "pomodoro_math.h"
#include "ui_pixel.h"
#include "ui_sfx.h"

#define POMODORO_NAMESPACE "pomodoro"
#define POMODORO_DONE_KEY  "done"

static lv_obj_t *s_screen;
static lv_obj_t *s_arc;
static lv_obj_t *s_clock;
static lv_obj_t *s_status;
static lv_obj_t *s_count;
static lv_obj_t *s_growth;
static lv_obj_t *s_pet;
static lv_obj_t *s_panels[2];
static lv_obj_t *s_labels[2];
static lv_timer_t *s_timer;
static uint64_t s_deadline_ms;
static uint32_t s_completed;
static int s_remaining = POMODORO_SESSION_SECONDS;
static int s_selection;
static int s_drawn_stage = -1;
static unsigned s_anim_frame;
static bool s_loaded;
static bool s_running;
static bool s_finished;

static lv_obj_t *pixel_box(lv_obj_t *parent, int x, int y, int w, int h,
                           uint32_t color)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(color), 0);
    return box;
}

static void draw_tomato(lv_obj_t *parent)
{
    const uint32_t red = 0xEF2B20;
    const uint32_t dark_red = 0xA81718;
    const uint32_t green = 0x28C840;
    const uint32_t dark_green = 0x087C38;

    pixel_box(parent, 26, 12, 38, 8, dark_green);
    pixel_box(parent, 38, 4, 10, 20, green);
    pixel_box(parent, 17, 16, 56, 9, green);
    pixel_box(parent, 8, 28, 74, 42, red);
    pixel_box(parent, 14, 22, 62, 56, red);
    pixel_box(parent, 21, 76, 48, 7, dark_red);
    pixel_box(parent, 7, 42, 8, 25, dark_red);
    pixel_box(parent, 68, 32, 9, 36, 0xD91D20);
    pixel_box(parent, 23, 32, 13, 9, 0xFF7D60);
    pixel_box(parent, 19, 41, 8, 9, 0xFFFFFF);
    pixel_box(parent, 56, 62, 10, 8, 0xC41418);
}

static void draw_pet(int stage)
{
    lv_obj_clean(s_pet);
    const uint32_t cream = 0xFFF1D0;
    const uint32_t orange = 0xFF8A28;
    const uint32_t brown = 0x713726;
    const uint32_t pink = 0xFF7890;

    if (stage == 0) {
        pixel_box(s_pet, 9, 10, 30, 22, cream);
        pixel_box(s_pet, 9, 5, 8, 10, orange);
        pixel_box(s_pet, 31, 5, 8, 10, orange);
        pixel_box(s_pet, 14, 16, 5, 7, brown);
        pixel_box(s_pet, 29, 16, 5, 7, brown);
        pixel_box(s_pet, 22, 24, 6, 4, pink);
        pixel_box(s_pet, 16, 32, 16, 6, orange);
    } else {
        pixel_box(s_pet, 8, 7, 32, 24, cream);
        pixel_box(s_pet, 8, 2, 9, 10, orange);
        pixel_box(s_pet, 31, 2, 9, 10, orange);
        pixel_box(s_pet, 13, 14, 5, 7, brown);
        pixel_box(s_pet, 30, 14, 5, 7, brown);
        pixel_box(s_pet, 21, 22, 7, 4, pink);
        pixel_box(s_pet, 13, 31, 24, 11, orange);
        pixel_box(s_pet, 5, 34, 10, 5, orange);
        pixel_box(s_pet, 16, 41, 6, 4, cream);
        pixel_box(s_pet, 29, 41, 6, 4, cream);
    }
    if (stage >= 2) {
        pixel_box(s_pet, 18, 0, 15, 4, UI_ACCENT);
        pixel_box(s_pet, 21, -4, 4, 7, UI_ACCENT);
        pixel_box(s_pet, 28, -4, 4, 7, UI_ACCENT);
        pixel_box(s_pet, 42, 4, 5, 5, pink);
        pixel_box(s_pet, 46, 8, 5, 5, pink);
        pixel_box(s_pet, 42, 12, 5, 5, pink);
        pixel_box(s_pet, 38, 8, 5, 5, pink);
    }
}

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000;
}

static void load_progress(void)
{
    if (s_loaded) return;
    s_loaded = true;
    nvs_handle_t handle;
    if (nvs_open(POMODORO_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        (void)nvs_get_u32(handle, POMODORO_DONE_KEY, &s_completed);
        nvs_close(handle);
    }
}

static void save_progress(void)
{
    nvs_handle_t handle;
    if (nvs_open(POMODORO_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_u32(handle, POMODORO_DONE_KEY, s_completed) == ESP_OK) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
}

static void update_remaining(void)
{
    if (!s_running) return;
    s_remaining = pomodoro_seconds_left(now_ms(), s_deadline_ms);
}

static void complete_session(void)
{
    s_running = false;
    s_finished = true;
    s_remaining = 0;
    ++s_completed;
    save_progress();
    app_power_activity();
    app_voice_mode_t mode = app_settings_voice_mode();
    if (mode == APP_VOICE_SPEECH) ui_sfx_play(UI_SFX_DONE_SPEECH);
    else if (mode == APP_VOICE_TONE) ui_sfx_play(UI_SFX_CONFIRM);
}

static void pomodoro_render(void)
{
    char text[64];
    snprintf(text, sizeof(text), "%02d:%02d",
             s_remaining / 60, s_remaining % 60);
    lv_label_set_text(s_clock, text);
    lv_arc_set_value(s_arc,
                     pomodoro_progress_percent(s_remaining));

    if (s_finished) lv_label_set_text(s_status, "专注完成 宠物成长");
    else if (s_running) lv_label_set_text(s_status, "专注进行中");
    else if (s_remaining < POMODORO_SESSION_SECONDS) {
        lv_label_set_text(s_status, "计时已暂停");
    } else {
        lv_label_set_text(s_status, "准备开始");
    }

    snprintf(text, sizeof(text), "完成 %lu 次  伙伴 %d级",
             (unsigned long)s_completed,
             pomodoro_pet_stage(s_completed) + 1);
    lv_label_set_text(s_count, text);
    lv_bar_set_value(s_growth, pomodoro_stage_progress(s_completed),
                     LV_ANIM_OFF);

    if (s_finished) lv_label_set_text(s_labels[0], "再来一轮");
    else if (s_running) lv_label_set_text(s_labels[0], "暂停");
    else if (s_remaining < POMODORO_SESSION_SECONDS) {
        lv_label_set_text(s_labels[0], "继续");
    } else {
        lv_label_set_text(s_labels[0], "开始");
    }
    lv_label_set_text(s_labels[1], "重置");
    for (int i = 0; i < 2; ++i) {
        ui_pixel_set_selected(s_panels[i], s_selection == i, true);
    }

    int stage = pomodoro_pet_stage(s_completed);
    if (stage != s_drawn_stage) {
        draw_pet(stage);
        s_drawn_stage = stage;
    }
    lv_obj_set_y(s_pet, 216 + (s_running && (s_anim_frame & 1U) ? -2 : 0));
}

static void pomodoro_tick(lv_timer_t *timer)
{
    (void)timer;
    ++s_anim_frame;
    if (s_running) {
        update_remaining();
        if (s_remaining == 0) complete_session();
    }
    pomodoro_render();
}

static void start_or_pause(void)
{
    if (s_running) {
        update_remaining();
        s_running = false;
        return;
    }
    if (s_finished || s_remaining <= 0) {
        s_remaining = POMODORO_SESSION_SECONDS;
        s_finished = false;
    }
    s_deadline_ms = now_ms() + (uint64_t)s_remaining * 1000;
    s_running = true;
}

static void reset_session(void)
{
    s_running = false;
    s_finished = false;
    s_remaining = POMODORO_SESSION_SECONDS;
}

void app_pomodoro_enter(void)
{
    load_progress();
    update_remaining();
    if (s_running && s_remaining == 0) complete_session();

    s_selection = 0;
    s_drawn_stage = -1;
    s_screen = ui_pixel_bare_screen_create();

    lv_obj_t *title = ui_pixel_label(s_screen, "口袋番茄钟",
                                     ui_menu_font(), UI_WHITE);
    lv_obj_set_width(title, 240);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(title, 0, 5);

    s_arc = lv_arc_create(s_screen);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_arc, 39, 31);
    lv_obj_set_size(s_arc, 162, 162);
    lv_arc_set_range(s_arc, 0, 100);
    lv_arc_set_bg_angles(s_arc, 130, 410);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(s_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x243143), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0xEF382C),
                               LV_PART_INDICATOR);

    lv_obj_t *tomato = lv_obj_create(s_screen);
    lv_obj_remove_flag(tomato, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(tomato, 75, 63);
    lv_obj_set_size(tomato, 90, 86);
    lv_obj_set_style_pad_all(tomato, 0, 0);
    lv_obj_set_style_border_width(tomato, 0, 0);
    lv_obj_set_style_bg_opa(tomato, LV_OPA_TRANSP, 0);
    draw_tomato(tomato);

    s_clock = ui_pixel_label(s_screen, "25:00", ui_menu_font(), UI_WHITE);
    lv_obj_set_width(s_clock, 100);
    lv_obj_set_style_text_align(s_clock, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_clock, 70, 166);
    s_status = ui_pixel_label(s_screen, "", ui_cn_font(), UI_PRIMARY);
    lv_obj_set_width(s_status, 180);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_status, 30, 192);

    s_count = ui_pixel_label(s_screen, "", ui_cn_font(), UI_WHITE);
    lv_obj_set_pos(s_count, 12, 218);
    lv_obj_t *play = ui_pixel_label(s_screen, "▶ PLAY", ui_cn_font(),
                                    UI_DANGER);
    lv_obj_set_pos(play, 12, 242);
    s_growth = lv_bar_create(s_screen);
    lv_obj_set_pos(s_growth, 75, 248);
    lv_obj_set_size(s_growth, 92, 8);
    lv_bar_set_range(s_growth, 0, 100);
    lv_obj_set_style_radius(s_growth, 0, 0);
    lv_obj_set_style_bg_color(s_growth, lv_color_hex(0x243143), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_growth, lv_color_hex(0x3DE35B),
                              LV_PART_INDICATOR);

    s_pet = lv_obj_create(s_screen);
    lv_obj_add_flag(s_pet, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_remove_flag(s_pet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_pet, 178, 216);
    lv_obj_set_size(s_pet, 55, 48);
    lv_obj_set_style_pad_all(s_pet, 0, 0);
    lv_obj_set_style_border_width(s_pet, 0, 0);
    lv_obj_set_style_bg_opa(s_pet, LV_OPA_TRANSP, 0);

    for (int i = 0; i < 2; ++i) {
        s_panels[i] = ui_pixel_panel_create(s_screen, 8 + i * 114, 274,
                                            110, 38, UI_SURFACE);
        s_labels[i] = ui_pixel_label(s_panels[i], "", ui_cn_font(), UI_INK);
        lv_obj_center(s_labels[i]);
    }

    s_timer = lv_timer_create(pomodoro_tick, 1000, NULL);
    pomodoro_render();
    lv_screen_load(s_screen);
}

void app_pomodoro_exit(void)
{
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_screen != NULL) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    s_arc = NULL;
    s_clock = NULL;
    s_status = NULL;
    s_count = NULL;
    s_growth = NULL;
    s_pet = NULL;
    for (int i = 0; i < 2; ++i) {
        s_panels[i] = NULL;
        s_labels[i] = NULL;
    }
}

void app_pomodoro_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (event != BSP_BTN_CLICK) return;
    if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
        s_selection = 1 - s_selection;
    } else if (button == BSP_BTN_OK && s_selection == 0) {
        start_or_pause();
    } else if (button == BSP_BTN_OK) {
        reset_session();
    }
    pomodoro_render();
}
