#include "app.h"

#include <stddef.h>

#include "bsp_battery.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "ui_pixel.h"
#include "ui_sfx.h"
#include "wireless_bridge.h"

static const uint8_t BRIGHTNESS[] = {25, 50, 75, 100};
static const uint8_t SFX_VOLUME[] = {0, 25, 50, 75, 100};
static const char *const VOICE_NAMES[] = {"静音", "提示音", "语音播报"};

static lv_obj_t *s_screen;
#define SETTING_COUNT 5

static lv_obj_t *s_panels[SETTING_COUNT];
static lv_obj_t *s_labels[SETTING_COUNT];
static lv_obj_t *s_hint;
static lv_timer_t *s_wifi_timer;
static int s_selection;
static bool s_network_selecting;
static int s_network_selection = 1;
static int s_brightness_index = 2;
static int s_sfx_volume_index = 2;
static app_voice_mode_t s_voice_mode = APP_VOICE_SPEECH;

static void settings_render(void)
{
    if (s_screen == NULL) {
        return;
    }
    if (wireless_bridge_is_provisioning()) {
        lv_label_set_text(s_labels[0], "手机连接临时热点");
        lv_label_set_text_fmt(s_labels[1], "%s",
                              wireless_bridge_setup_ssid());
        lv_label_set_text_fmt(s_labels[2], "密码  %s",
                              wireless_bridge_setup_password());
        lv_label_set_text(s_labels[3], "浏览器打开  192.168.4.1");
        if (wireless_bridge_provisioning_saved() &&
            wireless_bridge_is_connected()) {
            lv_label_set_text(s_labels[4], "连接成功    OK 关闭配网");
        } else if (wireless_bridge_provisioning_saved()) {
            lv_label_set_text(s_labels[4], "已保存    正在连接新网络");
        } else {
            lv_label_set_text(s_labels[4], "填入新网络后保存");
        }
        lv_label_set_text(s_hint, "完成后按 OK 关闭配网");
        for (int i = 0; i < SETTING_COUNT; ++i) {
            ui_pixel_set_selected(s_panels[i], i == 4, true);
        }
        return;
    }
    if (s_network_selecting) {
        char profile[33];
        lv_label_set_text(s_labels[0], "选择已保存的无线网络");
        for (int index = 0; index < 2; ++index) {
            if (wireless_bridge_profile_name(index, profile,
                                             sizeof(profile))) {
                lv_label_set_text_fmt(s_labels[index + 1], "%d  %s",
                                      index + 1, profile);
            } else {
                lv_label_set_text_fmt(s_labels[index + 1], "%d  尚未保存",
                                      index + 1);
            }
        }
        lv_label_set_text(s_labels[3], "手机添加新网络");
        lv_label_set_text(s_labels[4], "返回系统设置");
        lv_label_set_text(s_hint, "上/下选择    OK 切换");
        for (int i = 0; i < SETTING_COUNT; ++i) {
            ui_pixel_set_selected(s_panels[i], i == s_network_selection, true);
        }
        return;
    }

    lv_label_set_text_fmt(s_labels[0], "屏幕亮度    %d%%",
                          BRIGHTNESS[s_brightness_index]);
    lv_label_set_text_fmt(s_labels[1], "界面音效    %d%%",
                          SFX_VOLUME[s_sfx_volume_index]);
    lv_label_set_text_fmt(s_labels[2], "提醒方式    %s",
                          VOICE_NAMES[s_voice_mode]);
    char ip[16];
    wireless_bridge_get_ip(ip, sizeof(ip));
    lv_label_set_text_fmt(s_labels[3], "无线网络    %s",
                          wireless_bridge_is_connected() ? ip : "未连接");
    lv_label_set_text(s_labels[4], "Wi-Fi 管理（无需电脑）");
    int battery_soc = bsp_battery_soc();
    int battery_level = battery_soc >= 0 ? battery_soc : bsp_battery_level();
    int battery_soc_raw = bsp_battery_soc_raw();
    int battery_mv = bsp_battery_mv();
    int battery_config = bsp_battery_config();
    char soc_text[16];
    char mv_text[16];
    char raw_text[16];
    char config_text[16];
    if (battery_soc >= 0)        snprintf(soc_text, sizeof(soc_text), "%d%%", battery_soc);
    else if (battery_level >= 0) snprintf(soc_text, sizeof(soc_text), "~%d%%", battery_level);
    else                         snprintf(soc_text, sizeof(soc_text), "--%%");
    if (battery_mv >= 0)  snprintf(mv_text, sizeof(mv_text), "%dmV", battery_mv);
    else                  snprintf(mv_text, sizeof(mv_text), "--mV");
    if (battery_soc_raw >= 0) snprintf(raw_text, sizeof(raw_text), "S%04X",
                                       battery_soc_raw & 0xFFFF);
    else                      snprintf(raw_text, sizeof(raw_text), "S----");
    if (battery_config >= 0) snprintf(config_text, sizeof(config_text), "C%02X",
                                      battery_config & 0xFF);
    else                     snprintf(config_text, sizeof(config_text), "C--");
    lv_label_set_text_fmt(s_hint, "%s %s %s %s",
                          soc_text, mv_text, raw_text, config_text);
    for (int i = 0; i < SETTING_COUNT; ++i) {
        ui_pixel_set_selected(s_panels[i], s_selection == i, true);
    }
}

static void wifi_status_tick(lv_timer_t *timer)
{
    (void)timer;
    settings_render();
}

uint8_t app_settings_sfx_volume(void)
{
    return SFX_VOLUME[s_sfx_volume_index];
}

uint8_t app_settings_brightness(void)
{
    return BRIGHTNESS[s_brightness_index];
}

app_voice_mode_t app_settings_voice_mode(void)
{
    return s_voice_mode;
}

void app_settings_enter(void)
{
    s_selection = 0;
    s_network_selecting = false;
    s_network_selection = 1;
    s_screen = ui_pixel_screen_create("系统设置");
    for (int i = 0; i < SETTING_COUNT; ++i) {
        s_panels[i] = ui_pixel_panel_create(s_screen, 12, 50 + i * 46,
                                            216, 38, UI_SURFACE);
        s_labels[i] = ui_pixel_label(s_panels[i], "", ui_cn_font(), UI_INK);
        lv_obj_set_width(s_labels[i], 196);
        lv_label_set_long_mode(s_labels[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(s_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(s_labels[i]);
    }

    s_hint = ui_pixel_label(s_screen, "上/下选择    确认键执行",
                            ui_cn_font(), UI_MUTED);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -7);
    settings_render();
    s_wifi_timer = lv_timer_create(wifi_status_tick, 750, NULL);
    lv_screen_load(s_screen);
}

void app_settings_exit(void)
{
    if (s_wifi_timer != NULL) {
        lv_timer_delete(s_wifi_timer);
        s_wifi_timer = NULL;
    }
    if (wireless_bridge_is_provisioning()) {
        wireless_bridge_stop_provisioning();
    }
    if (s_screen != NULL) {
        lv_obj_delete(s_screen);
    }
    s_screen = NULL;
    s_hint = NULL;
    s_network_selecting = false;
    for (int i = 0; i < SETTING_COUNT; ++i) {
        s_panels[i] = NULL;
        s_labels[i] = NULL;
    }
}

void app_settings_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (event != BSP_BTN_CLICK) {
        return;
    }
    if (wireless_bridge_is_provisioning()) {
        if (button == BSP_BTN_OK) {
            wireless_bridge_stop_provisioning();
            s_network_selecting = true;
            s_network_selection = 1;
            settings_render();
        }
        return;
    }
    if (s_network_selecting) {
        if (button == BSP_BTN_UP) {
            s_network_selection = s_network_selection == 1
                                      ? 4 : s_network_selection - 1;
        } else if (button == BSP_BTN_DOWN) {
            s_network_selection = s_network_selection == 4
                                      ? 1 : s_network_selection + 1;
        } else if (button == BSP_BTN_OK && s_network_selection <= 2) {
            if (wireless_bridge_select_profile(s_network_selection - 1) == ESP_OK) {
                s_network_selecting = false;
                s_selection = 3;
            } else {
                lv_label_set_text(s_hint, "该网络尚未保存");
                return;
            }
        } else if (button == BSP_BTN_OK && s_network_selection == 3) {
            if (wireless_bridge_start_provisioning() != ESP_OK) {
                lv_label_set_text(s_hint, "首次配对需先连接一次电脑");
                return;
            }
        } else if (button == BSP_BTN_OK && s_network_selection == 4) {
            s_network_selecting = false;
            s_selection = 4;
        }
        settings_render();
        return;
    }
    if (button == BSP_BTN_UP) {
        s_selection = (s_selection + SETTING_COUNT - 1) % SETTING_COUNT;
    } else if (button == BSP_BTN_DOWN) {
        s_selection = (s_selection + 1) % SETTING_COUNT;
    } else if (button == BSP_BTN_OK && s_selection == 0) {
        s_brightness_index = (s_brightness_index + 1) % 4;
        bsp_display_backlight(BRIGHTNESS[s_brightness_index]);
    } else if (button == BSP_BTN_OK && s_selection == 1) {
        s_sfx_volume_index = (s_sfx_volume_index + 1) % 5;
        ui_sfx_set_volume(SFX_VOLUME[s_sfx_volume_index]);
    } else if (button == BSP_BTN_OK && s_selection == 2) {
        s_voice_mode = (app_voice_mode_t)((s_voice_mode + 1) % 3);
    } else if (button == BSP_BTN_OK && s_selection == 3) {
        wireless_bridge_reconnect();
    } else if (button == BSP_BTN_OK && s_selection == 4) {
        s_network_selecting = true;
        s_network_selection = 1;
    }
    settings_render();
}
