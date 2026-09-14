#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef void (*wireless_bridge_line_cb_t)(const char *line);
typedef void (*wireless_bridge_status_cb_t)(const char *status, const char *ip);

esp_err_t wireless_bridge_init(wireless_bridge_line_cb_t line_cb,
                               wireless_bridge_status_cb_t status_cb);
esp_err_t wireless_bridge_configure(const char *ssid, const char *password,
                                    const char *token);
bool wireless_bridge_profile_name(int index, char *buffer, size_t size);
esp_err_t wireless_bridge_select_profile(int index);
bool wireless_bridge_is_connected(void);
void wireless_bridge_get_ip(char *buffer, size_t size);
esp_err_t wireless_bridge_reconnect(void);
esp_err_t wireless_bridge_forget(void);
esp_err_t wireless_bridge_start_provisioning(void);
esp_err_t wireless_bridge_stop_provisioning(void);
bool wireless_bridge_is_provisioning(void);
bool wireless_bridge_provisioning_saved(void);
const char *wireless_bridge_setup_ssid(void);
const char *wireless_bridge_setup_password(void);
bool wireless_bridge_write(const char *data, size_t length);
