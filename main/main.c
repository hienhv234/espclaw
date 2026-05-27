/*
 * ESPClaw - main.c (Step 6: tool system)
 *
 * What this does:
 *   1. Print banner
 *   2. Init NVS
 *   3. Connect WiFi
 *   4. Init LLM provider (from NVS/menuconfig)
 *   5. Init GPIO HAL
 *   6. Init message bus + start serial channel
 *   7. Start agent loop (ReAct: calls LLM, dispatches tools, loops)
 *
 * Verify: idf.py build flash monitor
 * Try typing:
 *   espclaw> what is 2+2?   → LLM replies
 *   espclaw> heap           → Free heap: XXXXX bytes
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"

#include "platform.h"
#include "config.h"
#include "mem/nvs_manager.h"
#include "net/wifi_manager.h"
#include "bus/message_bus.h"
#include "channel/channel.h"
#include "nvs_keys.h"
#include "provider/provider.h"
#include "agent/agent_loop.h"
#include "agent/persona.h"
#include "hal/hal_gpio.h"
#include "service/cron_service.h"
#include "service/reminder_service.h"
#include "util/ratelimit.h"
#include "net/neuron_link.h"
#include "mem/neuron_cache.h"
#include "net/wifi_ap.h"
#include "display_ui.h"
#include <string.h>
#if ESPCLAW_HAS_WAKEWORD
#include "wakeword/wakeword.h"
#endif
#include "mbedtls/md5.h"

static void generate_deterministic_tenant_id(const char *device_id, char *out_uuid) {
    unsigned char digest[16];
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts(&ctx);
    mbedtls_md5_update(&ctx, (const unsigned char *)device_id, strlen(device_id));
    mbedtls_md5_finish(&ctx, digest);
    mbedtls_md5_free(&ctx);

    // Format to standard UUID v4 style
    digest[6] = (digest[6] & 0x0f) | 0x40; // Set version 4
    digest[8] = (digest[8] & 0x3f) | 0x80; // Set variant 10xxxxxx

    snprintf(out_uuid, 37, 
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             digest[0], digest[1], digest[2], digest[3],
             digest[4], digest[5],
             digest[6], digest[7],
             digest[8], digest[9],
             digest[10], digest[11], digest[12], digest[13], digest[14], digest[15]);
}

static const char *TAG = "espclaw";
static message_bus_t s_bus;
static bool s_display_ok = false;
static char s_device_id[12] = {0}; /* GETAI-XXXXX\0 */

static void generate_device_id(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    /* Hash all 6 MAC bytes into a single 32-bit value */
    uint32_t hash = 0;
    for (int i = 0; i < 6; i++) {
        hash = hash * 31 + mac[i];
    }

    /* Convert to 5-char alphanumeric (0-9, A-Z = 36 symbols) */
    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char code[6];
    for (int i = 4; i >= 0; i--) {
        code[i] = charset[hash % 36];
        hash /= 36;
    }
    code[5] = '\0';

    snprintf(s_device_id, sizeof(s_device_id), "GETAI-%s", code);
    ESP_LOGI(TAG, "Device ID: %s", s_device_id);
}

const char *get_device_id(void)
{
    return s_device_id;
}

/* Global TLS mutex instance */
SemaphoreHandle_t g_tls_mutex = NULL;

/* WiFi IP callback - called when IP is obtained */
static void wifi_ip_callback(const char *ip_str, void *user_data)
{
    ESP_LOGI(TAG, "WiFi IP callback: %s", ip_str);
    if (s_display_ok) {
        display_ui_clear();
        display_ui_write(0, 0, "ESPClaw v0.2.0");
        display_ui_write(0, 2, "WiFi Connected!");
        display_ui_write(0, 3, "IP:");
        display_ui_write(20, 3, ip_str);
        display_ui_write(0, 5, "** READY **");
        display_ui_update();
    }
    (void)user_data;
}

static void sync_task(void *pvParameters) {
    neuron_link_full_sync();
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* Silence verbose logs */
    esp_log_level_set("mbedtls", ESP_LOG_NONE);
    esp_log_level_set("httpd", ESP_LOG_WARN);
    esp_log_level_set("wifi", ESP_LOG_WARN);

    /* 0. Generate Device ID from MAC */
    generate_device_id();

    /* 0.5 Display init (non-fatal) */
    esp_err_t display_err = display_ui_init();
    if (display_err != ESP_OK) {
        ESP_LOGW(TAG, "Display init failed - continuing without display");
        s_display_ok = false;
    } else {
        s_display_ok = true;
        display_ui_set_device_id(s_device_id);
        display_ui_set_state(DISPLAY_STATE_INIT);
    }

    /* 1. Banner */
    printf("\n");
    printf("=============================\n");
    printf("  ESPClaw v0.2.0\n");
    printf("  Target: %s\n", ESPCLAW_TARGET_NAME);
#if ESPCLAW_HAS_PSRAM
    printf("  Mode: dual-core + PSRAM\n");
#else
    printf("  Mode: dual-core (Internal RAM only)\n");
#endif
    printf("=============================\n\n");

    /* 2. NVS init */
    esp_err_t err = nvs_mgr_init();
    ESP_LOGI(TAG, "NVS init. Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return;
    }

    /* 2.5 TLS mutex init */
    espclaw_tls_init();

    /* 2.6 Rate limiter init */
    ratelimit_init();

    /* 2.6 Persona init (load from NVS) */
    persona_init();

    /* 2.7 Message bus - INITIALIZE EARLY to avoid OOM after other services start */
    err = message_bus_init(&s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Message bus init failed - aborting");
        return;
    }

    /* 2.75 Start agent EARLY to secure Internal RAM for its stack */
    err = agent_start(&s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Agent start failed - system unstable");
        /* Non-fatal for now, but dangerous */
    } else {
        ESP_LOGI(TAG, "Agent started. Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    }

    /* 2.7 Cron service init */
    err = cron_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cron init failed: %s", esp_err_to_name(err));
    }

    /* 3. WiFi connect - start AP config portal if no WiFi configured */
    if (display_err == ESP_OK) {
        display_ui_set_state(DISPLAY_STATE_CONNECTING);
    }
    
    /* Register WiFi IP callback before connecting */
    wifi_mgr_set_ip_callback(wifi_ip_callback, NULL);
    
    err = wifi_mgr_init_and_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi not connected. Starting config portal...");
        ESP_LOGI(TAG, "Connect to WiFi: ESPClaw-XXXXX");
        ESP_LOGI(TAG, "Password: espclaw1");
        ESP_LOGI(TAG, "Then open http://192.168.4.1 in browser");
        
        if (display_err == ESP_OK) {
            display_ui_set_state(DISPLAY_STATE_WIFI_CONFIG);
            display_ui_show_message("WiFi Config", "Connect to AP:", "ESPClaw-XXXXX");
        }
        
        /* Start AP config portal */
        wifi_ap_start();
        
        /* Wait for WiFi config to be saved */
        while (wifi_ap_is_active()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        /* Retry WiFi connection with new config */
        ESP_LOGI(TAG, "Retrying WiFi connection...");
        err = wifi_mgr_init_and_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "WiFi still not connected. Continuing anyway...");
        }
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected, shutting down Web Services to free memory...");
        http_server_stop();
        wifi_ap_stop();
        /* Switch to Station-only mode to save more RAM/Power */
        esp_wifi_set_mode(WIFI_MODE_STA);
    } else {
        /* HTTP server fallback: start after WiFi connects (or if AP was active) */
        if (!wifi_ap_is_active()) {
            http_server_start();
        }
    }

    if (display_err == ESP_OK) {
        display_ui_set_state(DISPLAY_STATE_CONNECTED);
    }

    /* 4. LLM provider (non-fatal: agent will show a helpful error per message) */
    err = provider_registry_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LLM not configured — set API key via menuconfig or provision.sh");
        if (display_err == ESP_OK) {
            display_ui_set_state(DISPLAY_STATE_LLM_CONFIG);
            char ip_url[32] = "http://<no ip>";
            char ip_tmp[16];
            if (wifi_mgr_get_ip_str(ip_tmp, sizeof(ip_tmp))) {
                snprintf(ip_url, sizeof(ip_url), "http://%s", ip_tmp);
            }
            display_ui_show_message("LLM Setup", "Visit:", ip_url);
        }
    } else {
        if (display_err == ESP_OK) {
            display_ui_set_state(DISPLAY_STATE_READY);
        }
    }

    /* 5. GPIO HAL */
    hal_gpio_init();

    /* 5.5 Neuron Link init (Supabase sync) */
    char nvs_sb_key[256] = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImpvcGhicnNic2ZtdGdmYmZydmp3Iiwicm9sZSI6InNlcnZpY2Vfcm9sZSIsImlhdCI6MTc3NzM3NjMwMSwiZXhwIjoyMDkyOTUyMzAxfQ.qm4V5d2uPFAEKahRwrOBkcctPA-Feg3K2TvJh81KBXs";
    char deterministic_tenant[37] = {0};
    
    /* Dynamically generate deterministic tenant UUID from device ID */
    generate_deterministic_tenant_id(s_device_id, deterministic_tenant);
    ESP_LOGI(TAG, "Using deterministic Tenant UUID: %s", deterministic_tenant);

    /* Clean old NVS keys to guarantee fallback to deterministic system identity */
    nvs_mgr_erase_key(NVS_NAMESPACE, "supabase_key");
    nvs_mgr_erase_key("ncache", "last_sync");
    
    /* Clean corrupted GPIO NVS keys from old boot loops */
    nvs_mgr_erase_key(NVS_NAMESPACE, "gpio_sda");
    nvs_mgr_erase_key(NVS_NAMESPACE, "gpio_scl");
    nvs_mgr_erase_key(NVS_NAMESPACE, "gpio_mic_sck");
    nvs_mgr_erase_key(NVS_NAMESPACE, "gpio_mic_ws");
    nvs_mgr_erase_key(NVS_NAMESPACE, "gpio_mic_sd");
    nvs_mgr_erase_key(NVS_NAMESPACE, "gpio_spk_bclk");
    nvs_mgr_erase_key(NVS_NAMESPACE, "gpio_spk_lrc");
    nvs_mgr_erase_key(NVS_NAMESPACE, "gpio_spk_dout");

    /* Store deterministic tenant ID in NVS so all pairing, sync, and status checks operate smoothly */
    nvs_mgr_set_str("tenant_id", deterministic_tenant);

    /* Try to load custom key from NVS (if set) */
    nvs_mgr_get_str("supabase_key", nvs_sb_key, sizeof(nvs_sb_key));

    nl_config_t nl_config = {
        .supabase_url = "https://jophbrsbsfmtgfbfrvjw.supabase.co",
        .supabase_key = nvs_sb_key,
        .device_id = s_device_id,
        .tenant_id = deterministic_tenant,
        .mode = SYNC_MODE_DELTA,
        .direction = SYNC_DIR_BIDIRECTIONAL,
        .batch_size = 50,
        .timeout_ms = 30000,
        .auto_sync = true,
        .auto_sync_interval_sec = 300,
    };
    
    err = neuron_link_init(&nl_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Neuron Link init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Neuron Link initialized");
        
        /* Reset local cache to force clean push of local configurations to the new tenant */
        neuron_cache_reset();
        
        /* Check if paired, trigger sync in background if so */
        if (neuron_link_is_paired()) {
            ESP_LOGI(TAG, "Device is paired, triggering background sync...");
            xTaskCreate(sync_task, "full_sync", 10240, NULL, 5, NULL);
        }
    }

    /* 6. Start agent - MOVED TO EARLY INIT */
    
    /* 6.5 Start cron service (needs agent queue for firing actions) */
    err = cron_start(s_bus.inbound);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cron service start failed: %s", esp_err_to_name(err));
    }

    /* 6.6 Start reminder service (polls backend for due reminders) */
    err = reminder_service_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Reminder service init failed: %s", esp_err_to_name(err));
    } else {
        err = reminder_service_start(s_bus.inbound);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Reminder service start failed: %s", esp_err_to_name(err));
        }
    }

    /* 7. Start channels (serial CLI, later: Telegram, WebSocket) */
    channel_registry_init();
    channel_start_all(&s_bus);

#if ESPCLAW_HAS_WAKEWORD
    err = wakeword_init(&s_bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wake word init failed: %s", esp_err_to_name(err));
    } else {
        err = wakeword_start();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Wake word start failed: %s", esp_err_to_name(err));
        } else if (s_display_ok && wakeword_is_enabled()) {
            display_ui_set_state(DISPLAY_STATE_LISTENING);
        }
    }
#endif

    /* 8. Start local web server for config (accessible via station IP or mDNS) */
    http_server_start();

    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "ESPClaw ready");
}
