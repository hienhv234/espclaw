/*
 * Stub implementation of esp_websocket_client for ESP-IDF v5.3 compatibility
 * Note: This is a placeholder - actual websocket functionality requires
 * either esp-idf v4.x websocket component or custom implementation
 */
#ifndef ESP_WEBSOCKET_CLIENT_H
#define ESP_WEBSOCKET_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *esp_websocket_client_handle_t;

typedef struct {
    const char *uri;
    const char *host;
    int port;
    const char *path;
    bool disable_auto_reconnect;
    void *user_context;
    int task_stack;
    int task_prio;
    bool keep_alive_enable;
    int keep_alive_interval;
    int keep_alive_idle;
    int ping_interval_sec;
} esp_websocket_client_config_t;

typedef enum {
    WEBSOCKET_EVENT_ANY = -1,
    WEBSOCKET_EVENT_ERROR = 0,
    WEBSOCKET_EVENT_CONNECTED = 1,
    WEBSOCKET_EVENT_DISCONNECTED = 2,
    WEBSOCKET_EVENT_DATA = 3,
    WEBSOCKET_EVENT_MAX
} websocket_event_ids_t;

typedef struct {
    int payload_len;
    int payload_offset;
    char *data;
    int data_len;
} esp_websocket_event_data_t;

esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *config);
esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client);
esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client);
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client);
esp_err_t esp_websocket_client_send_text(esp_websocket_client_handle_t client, const char *data, int len);
esp_err_t esp_websocket_client_send_bin(esp_websocket_client_handle_t client, const uint8_t *data, int len);
bool esp_websocket_client_in_use(esp_websocket_client_handle_t client);

#ifdef __cplusplus
}
#endif

#endif /* ESP_WEBSOCKET_CLIENT_H */
