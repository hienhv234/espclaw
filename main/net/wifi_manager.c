/*
 * ESPClaw - net/wifi_manager.c
 * WiFi STA connection. Reads SSID/password from NVS, falls back to Kconfig.
 */
#include "wifi_manager.h"
#include "mem/nvs_manager.h"
#include "nvs_keys.h"
#include "config.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi_mgr";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count = 0;
static bool s_connected = false;
static char s_ip_str[16] = {0};
static wifi_ip_callback_t s_ip_callback = NULL;
static void *s_ip_callback_arg = NULL;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Associated to AP, waiting for IP...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Disconnected, reason=%d", d->reason);
        // Log human-readable disconnect reason
        switch (d->reason) {
            case 1: ESP_LOGW(TAG, "  -> Unspecified reason"); break;
            case 2: ESP_LOGW(TAG, "  -> Auth expires"); break;
            case 3: ESP_LOGW(TAG, "  -> Deauthed by AP"); break;
            case 4: ESP_LOGW(TAG, "  -> AP not found"); break;
            case 5: ESP_LOGW(TAG, "  -> Auth rejected"); break;
            case 6: ESP_LOGW(TAG, "  -> BSS not in range"); break;
            case 7: ESP_LOGW(TAG, "  -> Auth leave AP"); break;
            case 8: ESP_LOGW(TAG, "  -> Auth not authed"); break;
            case 9: ESP_LOGW(TAG, "  -> Assoc not assoc"); break;
            case 10: ESP_LOGW(TAG, "  -> Power cap unacceptable"); break;
            case 11: ESP_LOGW(TAG, "  -> Supported channel unacceptable"); break;
            case 12: ESP_LOGW(TAG, "  -> BSS.transition"); break;
            case 13: ESP_LOGW(TAG, "  -> Invalid IE"); break;
            case 14: ESP_LOGW(TAG, "  -> MIC failure"); break;
            case 15: ESP_LOGW(TAG, "  -> 4-way timeout"); break;
            case 16: ESP_LOGW(TAG, "  -> Group key update timeout"); break;
            case 17: ESP_LOGW(TAG, "  -> IE in 4-way different from group/broad"); break;
            case 18: ESP_LOGW(TAG, "  -> Multicast cipher invalid"); break;
            case 19: ESP_LOGW(TAG, "  -> Unicast cipher invalid"); break;
            case 20: ESP_LOGW(TAG, "  -> AKMP invalid"); break;
            case 21: ESP_LOGW(TAG, "  -> Unsupported RSNE IE"); break;
            case 22: ESP_LOGW(TAG, "  -> 802.1X auth failed"); break;
            case 23: ESP_LOGW(TAG, "  -> Cipher suite rejected"); break;
            default: ESP_LOGW(TAG, "  -> Unknown reason code"); break;
        }
        s_connected = false;
        if (s_retry_count < WIFI_MAX_RETRY) {
            s_retry_count++;
            ESP_LOGW(TAG, "Retry %d/%d...", s_retry_count, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Connection failed after %d retries", WIFI_MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "WiFi connected, IP: %s", s_ip_str);
        s_retry_count = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        /* Call IP callback if registered */
        if (s_ip_callback) {
            s_ip_callback(s_ip_str, s_ip_callback_arg);
        }
    }
}

static bool s_initialized = false;

esp_err_t wifi_mgr_init_common(void)
{
    if (s_initialized) return ESP_OK;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_mgr_init_and_connect(void)
{
    char ssid[33] = {0};
    char pass[65] = {0};

    /* Try NVS first, then fall back to Kconfig */
    bool nvs_has_ssid = nvs_mgr_get_str(NVS_KEY_WIFI_SSID, ssid, sizeof(ssid));
    if (!nvs_has_ssid) {
#ifdef CONFIG_ESPCLAW_WIFI_SSID
        strncpy(ssid, CONFIG_ESPCLAW_WIFI_SSID, sizeof(ssid) - 1);
        ESP_LOGI(TAG, "Using WiFi SSID from Kconfig: '%s'", ssid);
#else
        ESP_LOGW(TAG, "No WiFi SSID found in NVS or Kconfig!");
#endif
    } else {
        ESP_LOGI(TAG, "Using WiFi SSID from NVS: '%s'", ssid);
    }
    
    bool nvs_has_pass = nvs_mgr_get_str(NVS_KEY_WIFI_PASS, pass, sizeof(pass));
    if (!nvs_has_pass) {
#ifdef CONFIG_ESPCLAW_WIFI_PASSWORD
        strncpy(pass, CONFIG_ESPCLAW_WIFI_PASSWORD, sizeof(pass) - 1);
#endif
    }

    if (strlen(ssid) == 0) {
        ESP_LOGE(TAG, "No WiFi SSID configured!");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Attempting to connect to SSID: '%s' (Pass: '%s')", ssid, pass[0] ? "********" : "NONE");

    s_wifi_event_group = xEventGroupCreate();

    /* Initialize network if not already done */
    ESP_ERROR_CHECK(wifi_mgr_init_common());
    
    /* Create default netif for STA if not exists */
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta_netif) {
        esp_netif_create_default_wifi_sta();
    }

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Wait for connection or failure */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

bool wifi_mgr_is_connected(void)
{
    return s_connected;
}

bool wifi_mgr_get_ip_str(char *buf, size_t len)
{
    if (!buf || len == 0) return false;
    if (s_ip_str[0] == '\0') return false;
    strncpy(buf, s_ip_str, len - 1);
    buf[len - 1] = '\0';
    return true;
}

void wifi_mgr_set_ip_callback(wifi_ip_callback_t callback, void *user_data)
{
    s_ip_callback = callback;
    s_ip_callback_arg = user_data;
}
