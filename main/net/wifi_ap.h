/*
 * ESPClaw - net/wifi_ap.h
 * WiFi AP mode for configuration portal
 */
#ifndef WIFI_AP_H
#define WIFI_AP_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Default AP settings */
#define DEFAULT_AP_SSID "ESPClaw-Config"
#define DEFAULT_AP_PASSWORD "espclaw1"
#define DEFAULT_AP_CHANNEL 1
#define DEFAULT_AP_MAX_CONNECTIONS 4

/* Config structure */
typedef struct {
    // WiFi
    char wifi_ssid[33];
    char wifi_password[65];
    
    // Telegram
    char tg_token[128];
    char tg_userid[32];
    
    // LLM
    char llm_type[16];     // "openai", "anthropic", "custom"
    char llm_api_key[256];
    char llm_base_url[256];
    char llm_model[64];
    
    // GPIO Config
    int gpio_sda;
    int gpio_scl;
    int gpio_mic_sck;
    int gpio_mic_ws;
    int gpio_mic_sd;
    int gpio_spk_bclk;
    int gpio_spk_lrc;
    int gpio_spk_dout;
    
    // System
    char tenant_id[40];
    char device_name[64];
    char mqtt_broker[128];
    uint16_t mqtt_port;
} espclaw_wifi_ap_config_t;

/* ============================================================
 * AP Mode
 * ============================================================ */

/**
 * Start AP mode with config portal
 * @return ESP_OK on success
 */
esp_err_t wifi_ap_start(void);

/**
 * Stop AP mode
 */
esp_err_t wifi_ap_stop(void);

/**
 * Check if AP is active
 */
bool wifi_ap_is_active(void);

/**
 * Get current config from portal
 */
const espclaw_wifi_ap_config_t* wifi_ap_get_config(void);

/**
 * Save config from portal to NVS
 */
esp_err_t wifi_ap_save_config(const espclaw_wifi_ap_config_t *config);

/**
 * Start only the HTTP server (on Station IP)
 */
esp_err_t http_server_start(void);

/**
 * Stop the HTTP server to free memory
 */
esp_err_t http_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_AP_H */
