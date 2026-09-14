#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_POWER_ACTIVE = 0,
    UI_POWER_DIMMED,
    UI_POWER_DISPLAY_OFF,
} ui_power_state_t;

int ui_pixel_blink_frame(uint32_t elapsed_ms);
int ui_pixel_jump_offset(unsigned frame);
size_t ui_pixel_text_page_count(const char *text, size_t chars_per_page);
size_t ui_pixel_text_page_copy(char *output, size_t output_size,
                               const char *text, size_t page,
                               size_t chars_per_page);
ui_power_state_t ui_power_state_for_idle(uint32_t idle_ms, bool keep_awake);
uint8_t ui_power_dim_backlight(uint8_t normal_percent);
