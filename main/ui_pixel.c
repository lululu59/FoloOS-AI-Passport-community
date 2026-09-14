#include "ui_pixel.h"

#include <stdio.h>

#include "bsp_battery.h"
#include "driver/usb_serial_jtag.h"
#include "ui_menu_chrome.h"

LV_FONT_DECLARE(lv_font_cn_16);
LV_FONT_DECLARE(lv_font_cn_20);

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *fill;
    lv_obj_t *label;
    lv_timer_t *timer;
} battery_ui_t;

static lv_obj_t *plain_box(lv_obj_t *parent, int x, int y, int w, int h,
                           uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

static void battery_update(lv_timer_t *timer)
{
    battery_ui_t *ui = lv_timer_get_user_data(timer);
    int soc = bsp_battery_soc();
    if (soc < 0 && bsp_battery_init() == ESP_OK) {
        soc = bsp_battery_soc();
    }
    if (soc < 0) {
        soc = bsp_battery_level();
    }
    bool usb_host = usb_serial_jtag_is_connected();
    uint32_t fill_color = UI_BORDER;
    uint32_t text_color = UI_WHITE;
    char text[12];

    if (soc < 0) {
        snprintf(text, sizeof(text), "--%%");
        soc = 0;
    } else if (soc <= 20) {
        fill_color = UI_DANGER;
        text_color = UI_DANGER;
        snprintf(text, sizeof(text), "%d%%", soc);
    } else if (usb_host) {
        fill_color = UI_ACCENT;
        text_color = UI_PRIMARY;
        snprintf(text, sizeof(text), "%d%%", soc);
    } else {
        fill_color = UI_ACCENT;
        text_color = UI_WHITE;
        snprintf(text, sizeof(text), "%d%%", soc);
    }

    int fill_width = soc * 18 / 100;
    if (soc > 0 && fill_width < 2) fill_width = 2;
    lv_obj_set_width(ui->fill, fill_width);
    lv_obj_set_style_bg_color(ui->fill, lv_color_hex(fill_color), 0);
    lv_label_set_text(ui->label, text);
    lv_obj_set_style_text_color(ui->label, lv_color_hex(text_color), 0);
}

static void screen_deleted(lv_event_t *event)
{
    battery_ui_t *ui = lv_event_get_user_data(event);
    if (ui->timer != NULL) {
        lv_timer_delete(ui->timer);
        ui->timer = NULL;
    }
    lv_free(ui);
}

const lv_font_t *ui_cn_font(void)
{
    return &lv_font_cn_16;
}

const lv_font_t *ui_menu_font(void)
{
    return &lv_font_cn_20;
}

lv_obj_t *ui_pixel_label(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *base_screen_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    return screen;
}

lv_obj_t *ui_pixel_bare_screen_create(void)
{
    return base_screen_create();
}

static void battery_attach(lv_obj_t *screen, lv_obj_t *parent,
                           int outline_x, int outline_y,
                           int label_x, int label_y, int label_w)
{
    battery_ui_t *battery = lv_malloc(sizeof(*battery));
    if (battery == NULL) {
        return;
    }
    battery->screen = screen;
    /* Five hard-edged pixel blocks.  Avoid LVGL borders/child coordinates:
     * both introduced uneven thickness on the 240 px physical panel. */
    plain_box(parent, outline_x + 2, outline_y + 2, 25, 14, 0x000000);
    plain_box(parent, outline_x + 24, outline_y + 4, 3, 6, UI_WHITE);
    plain_box(parent, outline_x, outline_y, 24, 14, UI_WHITE);
    plain_box(parent, outline_x + 2, outline_y + 2, 20, 10, UI_BG);
    battery->fill = plain_box(parent, outline_x + 3, outline_y + 3,
                              18, 8, UI_ACCENT);
    battery->label = ui_pixel_label(parent, "--%", ui_cn_font(), UI_MUTED);
    lv_obj_set_pos(battery->label, label_x, label_y);
    lv_obj_set_width(battery->label, label_w);
    lv_obj_set_style_text_align(battery->label, LV_TEXT_ALIGN_RIGHT, 0);
    battery->timer = lv_timer_create(battery_update, 5000, battery);
    lv_obj_add_event_cb(screen, screen_deleted, LV_EVENT_DELETE, battery);
    if (battery->timer != NULL) {
        battery_update(battery->timer);
    }
}

lv_obj_t *ui_pixel_screen_create(const char *title)
{
    lv_obj_t *screen = base_screen_create();

    lv_obj_t *header = plain_box(screen, 5, 5, 230, 38, UI_SURFACE);
    lv_obj_set_style_border_color(header, lv_color_hex(UI_PRIMARY), 0);
    lv_obj_set_style_border_width(header, 2, 0);
    lv_obj_t *heading = ui_pixel_label(header, title, ui_cn_font(), UI_PRIMARY);
    lv_obj_align(heading, LV_ALIGN_LEFT_MID, 7, 0);

    battery_attach(screen, header, 145, 13, 175, 11, 45);
    return screen;
}

lv_obj_t *ui_pixel_menu_screen_create(const char *title)
{
    lv_obj_t *screen = base_screen_create();
    lv_obj_t *chrome = lv_image_create(screen);
    lv_image_set_src(chrome, &ui_menu_chrome);
    lv_obj_set_pos(chrome, 0, 0);

    lv_obj_t *heading = ui_pixel_label(screen, title, ui_cn_font(), UI_PRIMARY);
    lv_obj_set_pos(heading, 14, 13);
    battery_attach(screen, screen, 158, 15, 188, 14, 44);
    return screen;
}

lv_obj_t *ui_pixel_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t color)
{
    lv_obj_t *panel = plain_box(parent, x, y, w, h, color);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_BORDER), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    return panel;
}

void ui_pixel_set_selected(lv_obj_t *panel, bool selected, bool enabled)
{
    uint32_t background = !enabled ? UI_DISABLED : UI_SURFACE;
    uint32_t foreground = selected ? UI_ACCENT : UI_INK;
    lv_obj_set_style_bg_color(panel, lv_color_hex(background), 0);
    lv_obj_set_style_border_color(panel,
                                  lv_color_hex(selected ? UI_ACCENT : UI_BORDER), 0);
    lv_obj_set_style_border_width(panel, selected ? 3 : 2, 0);

    uint32_t count = lv_obj_get_child_count(panel);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t *child = lv_obj_get_child(panel, (int32_t)i);
        lv_obj_set_style_text_color(child, lv_color_hex(foreground), 0);
    }
}
