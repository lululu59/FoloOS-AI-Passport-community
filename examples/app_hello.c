#include "app.h"
#include "ui_pixel.h"

static lv_obj_t *s_screen;

void app_hello_enter(void)
{
    s_screen = ui_pixel_screen_create("Hello");
    lv_obj_t *label = ui_pixel_label(s_screen,
        "Hello, FoloOS!\nOK: Home\nHold UP: Home", ui_cn_font(), UI_WHITE);
    lv_obj_center(label);
    lv_screen_load(s_screen);
}

void app_hello_exit(void)
{
    if (s_screen != NULL) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
}

void app_hello_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (event == BSP_BTN_CLICK && button == BSP_BTN_OK) {
        app_request_home();
    }
}
