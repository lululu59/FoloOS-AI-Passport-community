#pragma once

#include <stdbool.h>

#include "lvgl.h"

#define UI_BG       0x020817
#define UI_SURFACE  0x04122B
#define UI_WHITE    0xF5F7FF
#define UI_INK      0xF5F7FF
#define UI_MUTED    0x65A8C8
#define UI_BORDER   0x0B3A74
#define UI_DISABLED 0x142039
#define UI_PRIMARY  0x00D9FF
#define UI_ACCENT   0xA8FF00
#define UI_SELECTED 0x061B39
#define UI_SOFT     0x082451
#define UI_SUCCESS  0xA8FF00
#define UI_WARNING  0xFFB000
#define UI_DANGER   0xFF4D5A

const lv_font_t *ui_cn_font(void);
const lv_font_t *ui_menu_font(void);
lv_obj_t *ui_pixel_bare_screen_create(void);
lv_obj_t *ui_pixel_screen_create(const char *title);
lv_obj_t *ui_pixel_menu_screen_create(const char *title);
lv_obj_t *ui_pixel_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t color);
lv_obj_t *ui_pixel_label(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color);
void ui_pixel_set_selected(lv_obj_t *panel, bool selected, bool enabled);
