/*
 * Stub implementation of esp_websocket_client
 */
#include "esp_websocket_client.h"
#include "esp_log.h"

static const char *TAG = "websocket_stub";

esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *config) {
    ESP_LOGW(TAG, "esp_websocket_client is a stub - not functional in ESP-IDF v5.3");
    return NULL;
}

esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client) {
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client) {
    return ESP_OK;
}

esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client) {
    return ESP_OK;
}

esp_err_t esp_websocket_client_send_text(esp_websocket_client_handle_t client, const char *data, int len) {
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_websocket_client_send_bin(esp_websocket_client_handle_t client, const uint8_t *data, int len) {
    return ESP_ERR_NOT_SUPPORTED;
}

bool esp_websocket_client_in_use(esp_websocket_client_handle_t client) {
    return false;
}
