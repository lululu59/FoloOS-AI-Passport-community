#include <assert.h>
#include <string.h>
#include "ui_pixel_math.h"

int main(void)
{
    assert(ui_pixel_blink_frame(0) == 0);
    assert(ui_pixel_blink_frame(1700) == 1);
    assert(ui_pixel_blink_frame(1850) == 0);

    assert(ui_pixel_jump_offset(0) == 0);
    assert(ui_pixel_jump_offset(1) == -3);
    assert(ui_pixel_jump_offset(2) == -5);
    assert(ui_pixel_jump_offset(3) == -3);
    assert(ui_pixel_jump_offset(4) == 0);
    assert(ui_pixel_jump_offset(99) == 0);

    assert(ui_power_state_for_idle(29999, false) == UI_POWER_ACTIVE);
    assert(ui_power_state_for_idle(30000, false) == UI_POWER_DIMMED);
    assert(ui_power_state_for_idle(119999, false) == UI_POWER_DIMMED);
    assert(ui_power_state_for_idle(120000, false) == UI_POWER_DISPLAY_OFF);
    assert(ui_power_state_for_idle(999999, true) == UI_POWER_ACTIVE);
    assert(ui_power_dim_backlight(100) == 25);
    assert(ui_power_dim_backlight(75) == 22);
    assert(ui_power_dim_backlight(25) == 8);

    assert(ui_pixel_text_page_count("abcdef", 3) == 2);
    assert(ui_pixel_text_page_count("中文测试", 3) == 2);
    char page[32];
    assert(ui_pixel_text_page_copy(page, sizeof(page), "中文测试", 0, 3) > 0);
    assert(strcmp(page, "中文测") == 0);
    assert(ui_pixel_text_page_copy(page, sizeof(page), "中文测试", 1, 3) > 0);
    assert(strcmp(page, "试") == 0);
    return 0;
}
