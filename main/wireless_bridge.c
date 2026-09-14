#include "wireless_bridge.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"

#define BRIDGE_PORT 8765
#define DISCOVERY_PORT 8764
#define NETWORK_LINE_MAX 768
#define NETWORK_TASK_STACK 7168
#define TOKEN_MAX 64
#define SETUP_SSID "FoloOS-Setup"
#define SETUP_PASSWORD "folotoy88"
#define SETUP_FORM_MAX 256
#define WIFI_PROFILE_COUNT 2
#define WIFI_RECONFIG_RETRIES 40
#define WIFI_RETRY_PER_PROFILE 6
#define WIFI_RECOVERY_TASK_STACK 3072

static const char *TAG = "wireless_bridge";
static const char *NVS_NAMESPACE = "folo_bridge";
static const char *NVS_TOKEN_KEY = "token";
static const char *DISCOVERY_REQUEST = "FOLOOS_DISCOVER_V1";

static wireless_bridge_line_cb_t s_line_cb;
static wireless_bridge_status_cb_t s_status_cb;
static SemaphoreHandle_t s_client_lock;
static int s_client_fd = -1;
static bool s_client_authenticated;
static bool s_wifi_connected;
static bool s_has_credentials;
static volatile bool s_reconfiguring;
static char s_token[TOKEN_MAX + 1];
static char s_ip[16];
static wifi_config_t s_profiles[WIFI_PROFILE_COUNT];
static bool s_profile_valid[WIFI_PROFILE_COUNT];
static int s_active_profile = -1;
static int s_profile_failures;
static uint32_t s_profiles_tried;
static volatile bool s_recovery_pending;
static httpd_handle_t s_setup_server;
static bool s_provisioning;
static bool s_provision_saved;

static esp_err_t apply_station_config(wifi_config_t *config);

static void reset_profile_recovery(void)
{
    s_profile_failures = 0;
    s_profiles_tried = s_active_profile >= 0
                           ? 1U << (unsigned)s_active_profile
                           : 0U;
}

static int next_untried_profile(void)
{
    for (int offset = 1; offset <= WIFI_PROFILE_COUNT; ++offset) {
        int index = s_active_profile >= 0
                        ? (s_active_profile + offset) % WIFI_PROFILE_COUNT
                        : offset - 1;
        if (s_profile_valid[index] &&
            (s_profiles_tried & (1U << (unsigned)index)) == 0U) {
            return index;
        }
    }
    return -1;
}

static const char SETUP_PAGE[] =
    "<!doctype html><meta charset=utf-8><meta name=viewport "
    "content='width=device-width,initial-scale=1'><title>FoloOS 换网</title>"
    "<style>body{font:18px system-ui;max-width:420px;margin:40px auto;padding:20px}"
    "input,button{box-sizing:border-box;width:100%;padding:14px;margin:8px 0;font-size:18px}"
    "button{background:#1269ff;color:white;border:0;border-radius:8px}</style>"
    "<h2>FoloOS 更换 Wi-Fi</h2><form method=post action=/save>"
    "<label>Wi-Fi 名称</label><input name=ssid maxlength=32 required>"
    "<label>Wi-Fi 密码</label><input name=password type=password maxlength=64>"
    "<button type=submit>保存并连接</button></form>"
    "<p>只支持 2.4GHz。密码只会写入设备，不会发送到互联网。</p>";

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static void url_decode(char *destination, size_t size, const char *source)
{
    size_t out = 0U;
    for (size_t in = 0U; source[in] != '\0' && out + 1U < size; ++in) {
        if (source[in] == '+') {
            destination[out++] = ' ';
        } else if (source[in] == '%' && source[in + 1] != '\0' &&
                   source[in + 2] != '\0') {
            int high = hex_value(source[in + 1]);
            int low = hex_value(source[in + 2]);
            if (high >= 0 && low >= 0) {
                destination[out++] = (char)((high << 4) | low);
                in += 2U;
            }
        } else {
            destination[out++] = source[in];
        }
    }
    destination[out] = '\0';
}

static esp_err_t setup_page_get(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, SETUP_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t setup_save_post(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len >= SETUP_FORM_MAX) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid form");
    }
    char body[SETUP_FORM_MAX];
    int received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received,
                                   request->content_len - received);
        if (count <= 0) return ESP_FAIL;
        received += count;
    }
    body[received] = '\0';

    char encoded_ssid[128] = "";
    char encoded_password[192] = "";
    char ssid[33];
    char password[65];
    if (httpd_query_key_value(body, "ssid", encoded_ssid,
                              sizeof(encoded_ssid)) != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "missing ssid");
    }
    httpd_query_key_value(body, "password", encoded_password,
                          sizeof(encoded_password));
    url_decode(ssid, sizeof(ssid), encoded_ssid);
    url_decode(password, sizeof(password), encoded_password);
    esp_err_t result = wireless_bridge_configure(ssid, password, s_token);
    if (result != ESP_OK) {
        httpd_resp_set_type(request, "text/html; charset=utf-8");
        return httpd_resp_sendstr(request,
                                  "<meta charset=utf-8><h2>保存失败</h2>"
                                  "<p>请检查名称、密码长度和首次配对状态。</p>"
                                  "<a href='/'>返回</a>");
    }
    s_provision_saved = true;
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_sendstr(request,
                              "<meta charset=utf-8><h2>已经保存</h2>"
                              "<p>设备正在连接新 Wi-Fi。成功后回到设备按 OK 关闭配网。</p>");
}

static esp_err_t setup_server_start(void)
{
    if (s_setup_server != NULL) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 4096;
    esp_err_t result = httpd_start(&s_setup_server, &config);
    if (result != ESP_OK) return result;
    const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = setup_page_get,
    };
    const httpd_uri_t save = {
        .uri = "/save", .method = HTTP_POST, .handler = setup_save_post,
    };
    httpd_register_uri_handler(s_setup_server, &root);
    httpd_register_uri_handler(s_setup_server, &save);
    return ESP_OK;
}

static void client_replace(int new_fd)
{
    if (xSemaphoreTake(s_client_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        if (new_fd >= 0) close(new_fd);
        return;
    }
    if (s_client_fd >= 0) {
        shutdown(s_client_fd, SHUT_RDWR);
        close(s_client_fd);
    }
    s_client_fd = new_fd;
    s_client_authenticated = false;
    xSemaphoreGive(s_client_lock);
}

bool wireless_bridge_write(const char *data, size_t length)
{
    if (data == NULL || length == 0 || s_client_lock == NULL ||
        xSemaphoreTake(s_client_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return false;
    }
    bool complete = false;
    if (s_client_fd >= 0 && s_client_authenticated) {
        size_t sent = 0;
        while (sent < length) {
            int result = send(s_client_fd, data + sent, length - sent, 0);
            if (result <= 0) {
                shutdown(s_client_fd, SHUT_RDWR);
                break;
            }
            sent += (size_t)result;
        }
        complete = sent == length;
    }
    xSemaphoreGive(s_client_lock);
    return complete;
}

static bool authenticate_line(const char *line)
{
    cJSON *root = cJSON_Parse(line);
    const cJSON *type = root != NULL
                            ? cJSON_GetObjectItemCaseSensitive(root, "type")
                            : NULL;
    const cJSON *token = root != NULL
                             ? cJSON_GetObjectItemCaseSensitive(root, "token")
                             : NULL;
    bool valid = cJSON_IsString(type) && cJSON_IsString(token) &&
                 strcmp(type->valuestring, "bridge_auth") == 0 &&
                 s_token[0] != '\0' && strcmp(token->valuestring, s_token) == 0;
    cJSON_Delete(root);
    if (!valid) return false;

    if (xSemaphoreTake(s_client_lock, pdMS_TO_TICKS(500)) == pdTRUE) {
        s_client_authenticated = true;
        xSemaphoreGive(s_client_lock);
    }
    static const char reply[] = "{\"type\":\"bridge_auth_ok\"}\n";
    wireless_bridge_write(reply, sizeof(reply) - 1);
    return true;
}

static void process_client_bytes(const uint8_t *input, size_t count,
                                 char *line, size_t *line_length,
                                 bool *dropping_line)
{
    for (size_t index = 0; index < count; ++index) {
        char character = (char)input[index];
        if (character == '\r') continue;
        if (character == '\n') {
            if (!*dropping_line && *line_length > 0) {
                line[*line_length] = '\0';
                bool authenticated = false;
                if (xSemaphoreTake(s_client_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
                    authenticated = s_client_authenticated;
                    xSemaphoreGive(s_client_lock);
                }
                if (!authenticated) {
                    if (!authenticate_line(line)) client_replace(-1);
                } else if (s_line_cb != NULL) {
                    s_line_cb(line);
                }
            }
            *line_length = 0;
            *dropping_line = false;
        } else if (!*dropping_line) {
            if (*line_length < NETWORK_LINE_MAX) {
                line[(*line_length)++] = character;
            } else {
                *dropping_line = true;
            }
        }
    }
}

static int create_bound_socket(int type, int port)
{
    int fd = socket(AF_INET, type, IPPROTO_IP);
    if (fd < 0) return -1;
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void network_task(void *argument)
{
    (void)argument;
    int tcp_fd = create_bound_socket(SOCK_STREAM, BRIDGE_PORT);
    int udp_fd = create_bound_socket(SOCK_DGRAM, DISCOVERY_PORT);
    if (tcp_fd < 0 || udp_fd < 0 || listen(tcp_fd, 1) != 0) {
        ESP_LOGE(TAG, "network sockets failed: %d", errno);
        if (tcp_fd >= 0) close(tcp_fd);
        if (udp_fd >= 0) close(udp_fd);
        vTaskDelete(NULL);
        return;
    }

    char line[NETWORK_LINE_MAX + 1];
    size_t line_length = 0;
    bool dropping_line = false;
    uint8_t input[1024];
    ESP_LOGI(TAG, "LAN bridge ready on TCP %d", BRIDGE_PORT);

    while (true) {
        int client = -1;
        if (xSemaphoreTake(s_client_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            client = s_client_fd;
            xSemaphoreGive(s_client_lock);
        }
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(tcp_fd, &read_set);
        FD_SET(udp_fd, &read_set);
        int max_fd = tcp_fd > udp_fd ? tcp_fd : udp_fd;
        if (client >= 0) {
            FD_SET(client, &read_set);
            if (client > max_fd) max_fd = client;
        }
        struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
        int ready = select(max_fd + 1, &read_set, NULL, NULL, &timeout);
        if (ready < 0) continue;

        if (FD_ISSET(tcp_fd, &read_set)) {
            int accepted = accept(tcp_fd, NULL, NULL);
            if (accepted >= 0) {
                struct timeval send_timeout = {.tv_sec = 0, .tv_usec = 250000};
                setsockopt(accepted, SOL_SOCKET, SO_SNDTIMEO,
                           &send_timeout, sizeof(send_timeout));
                client_replace(accepted);
                line_length = 0;
                dropping_line = false;
                client = accepted;
            }
        }
        if (FD_ISSET(udp_fd, &read_set)) {
            struct sockaddr_in source;
            socklen_t source_length = sizeof(source);
            int count = recvfrom(udp_fd, input, sizeof(input) - 1, 0,
                                 (struct sockaddr *)&source, &source_length);
            if (count > 0) {
                input[count] = '\0';
                if (s_wifi_connected && s_token[0] != '\0' &&
                    strcmp((const char *)input, DISCOVERY_REQUEST) == 0) {
                    static const char reply[] =
                        "{\"device\":\"FoloOS-AI-Passport\",\"port\":8765}";
                    sendto(udp_fd, reply, sizeof(reply) - 1, 0,
                           (struct sockaddr *)&source, source_length);
                }
            }
        }
        if (client >= 0 && FD_ISSET(client, &read_set)) {
            int count = recv(client, input, sizeof(input), 0);
            if (count <= 0) {
                client_replace(-1);
                line_length = 0;
                dropping_line = false;
            } else {
                process_client_bytes(input, (size_t)count, line, &line_length,
                                     &dropping_line);
            }
        }
    }
}

static void wifi_recovery_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(150));

    if (!s_wifi_connected && !s_provisioning) {
        int next = next_untried_profile();
        if (next >= 0) {
            s_profiles_tried |= 1U << (unsigned)next;
            s_profile_failures = 0;
            ESP_LOGI(TAG, "trying saved Wi-Fi profile %d", next + 1);
            esp_err_t result = apply_station_config(&s_profiles[next]);
            if (result == ESP_OK) {
                s_active_profile = next;
            } else {
                ESP_LOGW(TAG, "saved Wi-Fi profile failed: %s",
                         esp_err_to_name(result));
                wireless_bridge_start_provisioning();
            }
        } else {
            ESP_LOGW(TAG, "saved Wi-Fi profiles unavailable; starting setup");
            wireless_bridge_start_provisioning();
        }
    }

    s_recovery_pending = false;
    vTaskDelete(NULL);
}

static void schedule_wifi_recovery(void)
{
    if (s_recovery_pending) return;
    s_recovery_pending = true;
    if (xTaskCreate(wifi_recovery_task, "wifi_recovery",
                    WIFI_RECOVERY_TASK_STACK, NULL, 4, NULL) != pdPASS) {
        s_recovery_pending = false;
        ESP_LOGW(TAG, "could not start Wi-Fi recovery task");
        esp_wifi_connect();
    }
}

static void wifi_event(void *argument, esp_event_base_t event_base,
                       int32_t event_id, void *event_data)
{
    (void)argument;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_has_credentials) esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        s_ip[0] = '\0';
        client_replace(-1);
        if (s_has_credentials && !s_reconfiguring && !s_provisioning &&
            !s_recovery_pending) {
            ++s_profile_failures;
            if (s_profile_failures < WIFI_RETRY_PER_PROFILE) {
                esp_wifi_connect();
            } else {
                schedule_wifi_recovery();
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        char ip[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip, sizeof(ip));
        strlcpy(s_ip, ip, sizeof(s_ip));
        s_wifi_connected = true;
        reset_profile_recovery();
        ESP_LOGI(TAG, "Wi-Fi connected: %s", ip);
        if (s_status_cb != NULL) s_status_cb("connected", ip);
    }
}

static void load_token(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    size_t length = sizeof(s_token);
    if (nvs_get_str(handle, NVS_TOKEN_KEY, s_token, &length) != ESP_OK) {
        s_token[0] = '\0';
    }
    nvs_close(handle);
}

static esp_err_t save_token(const char *token)
{
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result == ESP_OK) result = nvs_set_str(handle, NVS_TOKEN_KEY, token);
    if (result == ESP_OK) result = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    return result;
}

static void profile_key(char *buffer, size_t size, const char *prefix,
                        int index)
{
    snprintf(buffer, size, "%s%d", prefix, index);
}

static void copy_wifi_text(char *destination, size_t destination_size,
                           const uint8_t *source, size_t source_size)
{
    size_t length = strnlen((const char *)source, source_size);
    if (length >= destination_size) length = destination_size - 1U;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static void load_profiles(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    for (int index = 0; index < WIFI_PROFILE_COUNT; ++index) {
        char ssid_key[8];
        char pass_key[8];
        char ssid[33];
        char password[65];
        size_t ssid_size = sizeof(ssid);
        size_t password_size = sizeof(password);
        profile_key(ssid_key, sizeof(ssid_key), "ssid", index);
        profile_key(pass_key, sizeof(pass_key), "pass", index);
        if (nvs_get_str(handle, ssid_key, ssid, &ssid_size) != ESP_OK ||
            ssid[0] == '\0') {
            continue;
        }
        if (nvs_get_str(handle, pass_key, password, &password_size) != ESP_OK) {
            password[0] = '\0';
        }
        memset(&s_profiles[index], 0, sizeof(s_profiles[index]));
        memcpy(s_profiles[index].sta.ssid, ssid,
               strnlen(ssid, sizeof(s_profiles[index].sta.ssid)));
        memcpy(s_profiles[index].sta.password, password,
               strnlen(password, sizeof(s_profiles[index].sta.password)));
        s_profiles[index].sta.threshold.authmode =
            password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
        s_profile_valid[index] = true;
    }
    nvs_close(handle);
}

static int find_profile(const wifi_config_t *config)
{
    for (int index = 0; index < WIFI_PROFILE_COUNT; ++index) {
        if (s_profile_valid[index] &&
            strncmp((const char *)s_profiles[index].sta.ssid,
                    (const char *)config->sta.ssid,
                    sizeof(config->sta.ssid)) == 0) {
            return index;
        }
    }
    return -1;
}

static esp_err_t save_profile(int index, const wifi_config_t *config)
{
    char ssid[33];
    char password[65];
    char ssid_key[8];
    char pass_key[8];
    copy_wifi_text(ssid, sizeof(ssid), config->sta.ssid,
                   sizeof(config->sta.ssid));
    copy_wifi_text(password, sizeof(password), config->sta.password,
                   sizeof(config->sta.password));
    profile_key(ssid_key, sizeof(ssid_key), "ssid", index);
    profile_key(pass_key, sizeof(pass_key), "pass", index);

    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result == ESP_OK) result = nvs_set_str(handle, ssid_key, ssid);
    if (result == ESP_OK) result = nvs_set_str(handle, pass_key, password);
    if (result == ESP_OK) result = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    if (result == ESP_OK) {
        s_profiles[index] = *config;
        s_profile_valid[index] = true;
    }
    return result;
}

static esp_err_t remember_profile(const wifi_config_t *config)
{
    int index = find_profile(config);
    if (index < 0) {
        for (int candidate = 0; candidate < WIFI_PROFILE_COUNT; ++candidate) {
            if (!s_profile_valid[candidate]) {
                index = candidate;
                break;
            }
        }
    }
    if (index < 0) index = s_active_profile == 0 ? 1 : 0;
    esp_err_t result = save_profile(index, config);
    if (result == ESP_OK) s_active_profile = index;
    return result;
}

static esp_err_t apply_station_config(wifi_config_t *config)
{
    s_reconfiguring = true;
    esp_err_t result = esp_wifi_disconnect();
    if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_CONNECT) {
        s_reconfiguring = false;
        return result;
    }
    for (int attempt = 0; attempt < WIFI_RECONFIG_RETRIES; ++attempt) {
        result = esp_wifi_set_config(WIFI_IF_STA, config);
        if (result != ESP_ERR_WIFI_STATE) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (result == ESP_OK) {
        s_has_credentials = true;
        result = esp_wifi_connect();
    }
    s_reconfiguring = false;
    return result;
}

bool wireless_bridge_is_connected(void)
{
    return s_wifi_connected;
}

void wireless_bridge_get_ip(char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) return;
    strlcpy(buffer, s_ip, size);
}

esp_err_t wireless_bridge_reconnect(void)
{
    if (!s_has_credentials) return ESP_ERR_INVALID_STATE;
    reset_profile_recovery();
    if (s_active_profile >= 0 && s_profile_valid[s_active_profile]) {
        return apply_station_config(&s_profiles[s_active_profile]);
    }
    esp_wifi_disconnect();
    return esp_wifi_connect();
}

esp_err_t wireless_bridge_forget(void)
{
    wifi_config_t empty = {0};
    esp_err_t result = esp_wifi_disconnect();
    if (result == ESP_ERR_WIFI_NOT_CONNECT) result = ESP_OK;
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_STA, &empty);

    if (result != ESP_OK) return result;

    s_has_credentials = false;
    s_wifi_connected = false;
    s_ip[0] = '\0';
    client_replace(-1);
    if (s_status_cb != NULL) s_status_cb("forgotten", "");
    return ESP_OK;
}

esp_err_t wireless_bridge_start_provisioning(void)
{
    if (s_token[0] == '\0') return ESP_ERR_INVALID_STATE;
    if (s_provisioning) return ESP_OK;
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, SETUP_SSID, sizeof(ap.ap.ssid));
    strlcpy((char *)ap.ap.password, SETUP_PASSWORD, sizeof(ap.ap.password));
    ap.ap.ssid_len = strlen(SETUP_SSID);
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_err_t result = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (result == ESP_OK) result = setup_server_start();
    if (result != ESP_OK) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        return result;
    }
    s_provisioning = true;
    s_provision_saved = false;
    s_profile_failures = 0;
    if (s_status_cb != NULL) s_status_cb("provisioning", SETUP_SSID);
    return ESP_OK;
}

esp_err_t wireless_bridge_stop_provisioning(void)
{
    if (s_setup_server != NULL) {
        httpd_stop(s_setup_server);
        s_setup_server = NULL;
    }
    s_provisioning = false;
    s_provision_saved = false;
    esp_err_t result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result == ESP_OK && s_has_credentials && !s_wifi_connected) {
        reset_profile_recovery();
        result = esp_wifi_connect();
    }
    return result;
}

bool wireless_bridge_is_provisioning(void)
{
    return s_provisioning;
}

bool wireless_bridge_provisioning_saved(void)
{
    return s_provision_saved;
}

const char *wireless_bridge_setup_ssid(void)
{
    return SETUP_SSID;
}

const char *wireless_bridge_setup_password(void)
{
    return SETUP_PASSWORD;
}

esp_err_t wireless_bridge_configure(const char *ssid, const char *password,
                                    const char *token)
{
    size_t ssid_length = ssid != NULL ? strnlen(ssid, 33) : 0;
    size_t password_length = password != NULL ? strnlen(password, 65) : 0;
    size_t token_length = token != NULL ? strnlen(token, TOKEN_MAX + 1) : 0;
    if (ssid_length == 0 || ssid_length > 32 || password_length > 64 ||
        (password_length > 0 && password_length < 8) ||
        token_length < 16 || token_length > TOKEN_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, ssid_length);
    memcpy(config.sta.password, password, password_length);
    config.sta.threshold.authmode = password_length == 0
                                        ? WIFI_AUTH_OPEN
                                        : WIFI_AUTH_WPA2_PSK;
    esp_err_t result = apply_station_config(&config);
    if (result != ESP_OK) return result;
    result = save_token(token);
    if (result != ESP_OK) return result;
    result = remember_profile(&config);
    if (result != ESP_OK) return result;

    strlcpy(s_token, token, sizeof(s_token));
    reset_profile_recovery();
    return ESP_OK;
}

bool wireless_bridge_profile_name(int index, char *buffer, size_t size)
{
    if (buffer == NULL || size == 0 || index < 0 ||
        index >= WIFI_PROFILE_COUNT || !s_profile_valid[index]) {
        if (buffer != NULL && size > 0) buffer[0] = '\0';
        return false;
    }
    copy_wifi_text(buffer, size, s_profiles[index].sta.ssid,
                   sizeof(s_profiles[index].sta.ssid));
    return true;
}

esp_err_t wireless_bridge_select_profile(int index)
{
    if (index < 0 || index >= WIFI_PROFILE_COUNT ||
        !s_profile_valid[index]) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = apply_station_config(&s_profiles[index]);
    if (result == ESP_OK) {
        s_active_profile = index;
        reset_profile_recovery();
    }
    return result;
}

esp_err_t wireless_bridge_init(wireless_bridge_line_cb_t line_cb,
                               wireless_bridge_status_cb_t status_cb)
{
    s_line_cb = line_cb;
    s_status_cb = status_cb;
    s_client_lock = xSemaphoreCreateMutex();
    if (s_client_lock == NULL) return ESP_ERR_NO_MEM;

    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        result = nvs_flash_erase();
        if (result == ESP_OK) result = nvs_flash_init();
    }
    if (result != ESP_OK) return result;
    if ((result = esp_netif_init()) != ESP_OK) return result;
    if ((result = esp_event_loop_create_default()) != ESP_OK) return result;
    if (esp_netif_create_default_wifi_sta() == NULL ||
        esp_netif_create_default_wifi_ap() == NULL) return ESP_FAIL;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((result = esp_wifi_init(&init)) != ESP_OK) return result;
    if ((result = esp_wifi_set_storage(WIFI_STORAGE_FLASH)) != ESP_OK) return result;
    if ((result = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             wifi_event, NULL)) != ESP_OK) return result;
    if ((result = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             wifi_event, NULL)) != ESP_OK) return result;
    if ((result = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return result;

    wifi_config_t stored = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &stored) == ESP_OK) {
        s_has_credentials = stored.sta.ssid[0] != '\0';
    }
    load_token();
    load_profiles();
    if (s_has_credentials) {
        int stored_profile = find_profile(&stored);
        if (stored_profile >= 0) {
            s_active_profile = stored_profile;
        } else if (remember_profile(&stored) != ESP_OK) {
            ESP_LOGW(TAG, "could not preserve current Wi-Fi profile");
        }
    }
    reset_profile_recovery();
    if ((result = esp_wifi_start()) != ESP_OK) return result;
    /* Modem-sleep keeps the TCP bridge reachable while allowing the radio to
     * sleep between access-point beacons.  Light/deep sleep is intentionally
     * not used because approvals must still arrive without reconnecting. */
    if ((result = esp_wifi_set_ps(WIFI_PS_MIN_MODEM)) != ESP_OK) return result;
    if (xTaskCreate(network_task, "wireless_bridge", NETWORK_TASK_STACK,
                    NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
