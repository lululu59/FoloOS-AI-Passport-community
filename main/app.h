#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bsp_button.h"
#include "lvgl.h"

typedef struct {
    const char *name;
    const lv_image_dsc_t *icon;
    void (*enter)(void);
    void (*exit)(void);
    void (*key)(bsp_btn_t button, bsp_btn_ev_t event);
} app_entry_t;

typedef enum {
    APP_VOICE_SILENT = 0,
    APP_VOICE_TONE,
    APP_VOICE_SPEECH,
} app_voice_mode_t;

void app_request_home(void);
void app_power_activity(void);
void app_power_hold_awake(bool keep_awake);

void app_coding_set_audio_available(bool available);
void app_coding_bridge_connected(bool connected);
void app_coding_bridge_transcript(const char *text);
void app_coding_bridge_task_queued(const char *text);
void app_coding_bridge_task_status(const char *text);
void app_coding_bridge_approval(const char *question, const char *detail);
void app_coding_bridge_done(const char *text);
void app_coding_bridge_error(const char *text);
void app_coding_catalog_begin(const char *scope);
void app_coding_catalog_item(const char *scope, const char *id,
                             const char *title, const char *detail);
void app_coding_catalog_end(const char *scope);
void app_coding_task_selected(const char *title, const char *project);
void app_coding_capture_level(uint8_t level);
bool app_coding_ok_is_record_action(void);
void app_coding_display_active(bool active);
void app_coding_enter(void);
void app_coding_exit(void);
void app_coding_key(bsp_btn_t button, bsp_btn_ev_t event);

void app_pomodoro_enter(void);
void app_pomodoro_exit(void);
void app_pomodoro_key(bsp_btn_t button, bsp_btn_ev_t event);

void app_word_bear_enter(void);
void app_word_bear_exit(void);
void app_word_bear_key(bsp_btn_t button, bsp_btn_ev_t event);
void app_word_bear_set_day(uint32_t day);
void app_word_bear_sync_request(void);
void app_word_bear_restore_begin(unsigned count, uint32_t day, int active_group);
void app_word_bear_restore_item(int id, uint16_t learn_count,
                                uint16_t correct_count, uint16_t wrong_count,
                                uint32_t last_day, uint32_t due_day,
                                uint8_t correct_streak, uint8_t flags);
void app_word_bear_restore_end(void);

void app_settings_enter(void);
void app_settings_exit(void);
void app_settings_key(bsp_btn_t button, bsp_btn_ev_t event);
app_voice_mode_t app_settings_voice_mode(void);
uint8_t app_settings_sfx_volume(void);
uint8_t app_settings_brightness(void);
