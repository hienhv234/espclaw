/*
 * ESPClaw - neuron_link.c
 * Neuron Link Protocol - Sync ESP32 cache with Supabase
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include "neuron_link.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "mem/nvs_manager.h"
#include "mem/neuron_cache.h"
#include "cJSON.h"
#include "display_ui.h"
#include "nvs_keys.h"
#include "workflow/workflow_engine.h"
#include "esp_crt_bundle.h"
#include "provider/provider.h"

static const char *TAG = "neuron_link";

/* Default config */
#define DEFAULT_BATCH_SIZE 50
#define DEFAULT_TIMEOUT_MS 30000
#define DEFAULT_AUTO_SYNC_INTERVAL 300

/* HTTP headers */
#define HEADER_AUTH "apikey"
#define HEADER_PB_AUTH "Authorization: Bearer "

/* WebSocket topics */
#define WS_TOPIC_NODES "nodes"
#define WS_TOPIC_LINKS "links"
#define WS_TOPIC_PULSES "pulses"

/* Static state */
static nl_config_t s_config = {0};
static nl_status_t s_status = NL_STATUS_IDLE;
static nl_sync_result_t s_last_result = {0};
static bool s_initialized = false;
static bool s_ws_connected = false;

/* WebSocket is stubbed - not available in ESP-IDF v5.3 */
// static esp_websocket_client_handle_t s_ws_client = NULL;

/* HTTP response accumulator */
typedef struct {
    char *buffer;
    int length;
    int capacity;
} http_response_buffer_t;

/* HTTP event handler */
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", 
                     evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (evt->user_data && evt->data_len > 0) {
                http_response_buffer_t *res = (http_response_buffer_t *)evt->user_data;
                int new_len = res->length + evt->data_len;
                if (new_len >= res->capacity) {
                    int new_cap = res->capacity * 2;
                    if (new_cap < new_len + 1) new_cap = new_len + 256;
                    char *new_buf = realloc(res->buffer, new_cap);
                    if (!new_buf) {
                        ESP_LOGE(TAG, "OOM in response realloc");
                        return ESP_ERR_NO_MEM;
                    }
                    res->buffer = new_buf;
                    res->capacity = new_cap;
                }
                memcpy(res->buffer + res->length, evt->data, evt->data_len);
                res->length = new_len;
                res->buffer[new_len] = '\0';
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        default:
            break;
    }
    return ESP_OK;
}

/* ============================================================
 * Initialization
 * ============================================================ */

esp_err_t neuron_link_init(const nl_config_t *config) {
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing Neuron Link...");
    
    if (!config || !config->supabase_url || !config->device_id) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Copy config */
    s_config.supabase_url = strdup(config->supabase_url);
    s_config.supabase_key = config->supabase_key ? strdup(config->supabase_key) : NULL;
    s_config.device_id = strdup(config->device_id);
    s_config.tenant_id = config->tenant_id ? strdup(config->tenant_id) : NULL;
    s_config.mode = config->mode;
    s_config.direction = config->direction;
    s_config.batch_size = config->batch_size ?: DEFAULT_BATCH_SIZE;
    s_config.timeout_ms = config->timeout_ms ?: DEFAULT_TIMEOUT_MS;
    s_config.auto_sync = config->auto_sync;
    s_config.auto_sync_interval_sec = config->auto_sync_interval_sec ?: DEFAULT_AUTO_SYNC_INTERVAL;
    
    /* Initialize cache */
    esp_err_t err = neuron_cache_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cache init failed: %s", esp_err_to_name(err));
    }
    
    s_initialized = true;
    s_status = NL_STATUS_IDLE;
    
    ESP_LOGI(TAG, "Neuron Link initialized");
    ESP_LOGI(TAG, "  Supabase: %s", s_config.supabase_url);
    ESP_LOGI(TAG, "  Device: %s", s_config.device_id);
    
    return ESP_OK;
}

esp_err_t neuron_link_deinit(void) {
    if (!s_initialized) return ESP_OK;
    
    neuron_link_disconnect();
    neuron_cache_deinit();
    
    free(s_config.supabase_url);
    free(s_config.supabase_key);
    free(s_config.device_id);
    free(s_config.tenant_id);
    
    memset(&s_config, 0, sizeof(s_config));
    s_initialized = false;
    
    return ESP_OK;
}

esp_err_t neuron_link_set_tenant_id(const char *tenant_id) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (tenant_id) {
        if (s_config.tenant_id) free(s_config.tenant_id);
        s_config.tenant_id = strdup(tenant_id);
        nvs_mgr_set_str(NVS_KEY_TENANT_ID, tenant_id);
        ESP_LOGI(TAG, "Tenant ID updated and persisted to NVS: %s", tenant_id);
    }
    return ESP_OK;
}

esp_err_t neuron_link_set_config(const nl_config_t *config) {
    if (!s_initialized || !config) return ESP_ERR_INVALID_ARG;
    
    /* Update fields */
    if (config->supabase_url) {
        free(s_config.supabase_url);
        s_config.supabase_url = strdup(config->supabase_url);
    }
    if (config->supabase_key) {
        free(s_config.supabase_key);
        s_config.supabase_key = strdup(config->supabase_key);
    }
    if (config->tenant_id) {
        free(s_config.tenant_id);
        s_config.tenant_id = strdup(config->tenant_id);
    }
    s_config.mode = config->mode;
    s_config.direction = config->direction;
    s_config.batch_size = config->batch_size ?: DEFAULT_BATCH_SIZE;
    s_config.timeout_ms = config->timeout_ms ?: DEFAULT_TIMEOUT_MS;
    s_config.auto_sync = config->auto_sync;
    s_config.auto_sync_interval_sec = config->auto_sync_interval_sec ?: DEFAULT_AUTO_SYNC_INTERVAL;
    
    return ESP_OK;
}

const nl_config_t* neuron_link_get_config(void) {
    return &s_config;
}

/* ============================================================
 * Connection
 * ============================================================ */

bool neuron_link_is_connected(void) {
    return s_ws_connected;
}

esp_err_t neuron_link_connect(void) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    s_status = NL_STATUS_CONNECTING;
    
    /* Check WiFi */
    // TODO: Check wifi_mgr status
    
    s_status = NL_STATUS_IDLE;
    return ESP_OK;
}

esp_err_t neuron_link_disconnect(void) {
    /* WebSocket is stubbed - not available in ESP-IDF v5.3 */
    // if (s_ws_client) {
    //     esp_websocket_client_stop(s_ws_client);
    //     esp_websocket_client_destroy(s_ws_client);
    //     s_ws_client = NULL;
    // }
    s_ws_connected = false;
    s_status = NL_STATUS_IDLE;
    
    return ESP_OK;
}

/* ============================================================
 * HTTP Helpers
 * ============================================================ */

static esp_err_t http_post(const char *endpoint, const char *body, char **out_response, int *out_status) {
    if (!s_initialized || !endpoint) return ESP_ERR_INVALID_ARG;
    
    char url[512];
    snprintf(url, sizeof(url), "%s%s", s_config.supabase_url, endpoint);
    
    http_response_buffer_t res_buf = {0};
    if (out_response) {
        res_buf.capacity = 1024;
        res_buf.buffer = malloc(res_buf.capacity);
        if (res_buf.buffer) {
            res_buf.buffer[0] = '\0';
        }
    }
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = out_response ? &res_buf : NULL,
        .timeout_ms = s_config.timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        if (res_buf.buffer) free(res_buf.buffer);
        return ESP_FAIL;
    }
    
    /* Headers */
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "apikey", s_config.supabase_key ?: "");
    
    char auth_hdr[300] = "";
    if (s_config.supabase_key) {
        snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", s_config.supabase_key);
    }
    esp_http_client_set_header(client, "Authorization", auth_hdr);
    esp_http_client_set_header(client, "Prefer", "resolution=merge-duplicates");
    
    if (body) {
        esp_http_client_set_post_field(client, body, strlen(body));
    }
    
    esp_err_t err = esp_http_client_perform(client);
    int status = 0;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
        if (out_status) *out_status = status;
        
        if (status < 200 || status >= 300) {
            ESP_LOGW(TAG, "http_post failed: endpoint=%s, status=%d", endpoint, status);
            if (res_buf.buffer && res_buf.length > 0) {
                ESP_LOGW(TAG, "Failure response: %s", res_buf.buffer);
            }
        } else {
            ESP_LOGI(TAG, "http_post success: endpoint=%s, status=%d", endpoint, status);
        }
        
        if (out_response && status >= 200 && status < 300) {
            *out_response = res_buf.buffer;
            res_buf.buffer = NULL; // prevent free
        }
    } else {
        ESP_LOGE(TAG, "http_post perform failed: endpoint=%s, err=%s", endpoint, esp_err_to_name(err));
    }
    
    if (res_buf.buffer) free(res_buf.buffer);
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t http_get(const char *endpoint, char **out_response, int *out_status) {
    if (!s_initialized || !endpoint) return ESP_ERR_INVALID_ARG;
    
    char url[512];
    snprintf(url, sizeof(url), "%s%s", s_config.supabase_url, endpoint);
    
    http_response_buffer_t res_buf = {0};
    if (out_response) {
        res_buf.capacity = 2048;
        res_buf.buffer = malloc(res_buf.capacity);
        if (res_buf.buffer) {
            res_buf.buffer[0] = '\0';
        }
    }
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = out_response ? &res_buf : NULL,
        .timeout_ms = s_config.timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        if (res_buf.buffer) free(res_buf.buffer);
        return ESP_FAIL;
    }
    
    /* Headers */
    esp_http_client_set_header(client, "apikey", s_config.supabase_key ?: "");
    
    char auth_hdr[300] = "";
    if (s_config.supabase_key) {
        snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", s_config.supabase_key);
    }
    esp_http_client_set_header(client, "Authorization", auth_hdr);
    
    /* Execute */
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (out_status) *out_status = status;
        
        if (out_response && status >= 200 && status < 300) {
            *out_response = res_buf.buffer;
            res_buf.buffer = NULL; // prevent free
        }
    }
    
    if (res_buf.buffer) free(res_buf.buffer);
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t http_delete(const char *endpoint, int *out_status) {
    if (!s_initialized || !endpoint) return ESP_ERR_INVALID_ARG;
    
    char url[512];
    snprintf(url, sizeof(url), "%s%s", s_config.supabase_url, endpoint);
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_DELETE,
        .event_handler = http_event_handler,
        .timeout_ms = s_config.timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;
    
    esp_http_client_set_header(client, "apikey", s_config.supabase_key ?: "");
    
    char auth_hdr[300] = "";
    if (s_config.supabase_key) {
        snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", s_config.supabase_key);
    }
    esp_http_client_set_header(client, "Authorization", auth_hdr);
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK && out_status) {
        *out_status = esp_http_client_get_status_code(client);
    }
    
    esp_http_client_cleanup(client);
    return err;
}

static void neuron_link_prepare_local_config(void) {
    if (!s_config.tenant_id || strlen(s_config.tenant_id) == 0) return;
    
    /* Local config preparation: only push to Cloud if local NVS values are present.
       This allows the AP portal to update the Dashboard. */
    ESP_LOGI(TAG, "Preparing local config for sync...");
#if 1
    /* 1. Telegram Interface */
    char token[128] = {0};
    char chat_id[64] = {0};
    if (nvs_mgr_get_str(NVS_KEY_TG_TOKEN, token, sizeof(token)) && strlen(token) > 0) {
        nvs_mgr_get_str(NVS_KEY_TG_CHAT_IDS, chat_id, sizeof(chat_id));
        cache_node_t cn = {0};
        strlcpy(cn.id, s_config.tenant_id, sizeof(cn.id));
        cn.id[0] = '1'; cn.id[1] = '1'; cn.id[2] = '1';
        strlcpy(cn.tenant_id, s_config.tenant_id, sizeof(cn.tenant_id));
        strcpy(cn.type, "webhook");
        strcpy(cn.subtype, "interface_telegram");
        strcpy(cn.name, "Telegram Interface");
        cn.is_active = true;
        cn.updated_at = (uint64_t)time(NULL);
        cn.content = cJSON_CreateObject();
        cJSON_AddStringToObject(cn.content, "bot_token", token);
        if (chat_id[0]) cJSON_AddStringToObject(cn.content, "chat_id", chat_id);
        neuron_cache_upsert_node(&cn);
        cJSON_Delete(cn.content);
        ESP_LOGI(TAG, "  -> Telegram config added to sync");
    }

    /* 2. LLM Config */
    char llm_type[16] = {0};
    char llm_key[256] = {0};
    if (nvs_mgr_get_str(NVS_KEY_LLM_BACKEND, llm_type, sizeof(llm_type)) && strlen(llm_type) > 0) {
        nvs_mgr_get_str(NVS_KEY_LLM_API_KEY, llm_key, sizeof(llm_key));
        cache_node_t cn = {0};
        strcpy(cn.id, "00000000-0000-0000-0000-000000000002");
        strlcpy(cn.tenant_id, s_config.tenant_id, sizeof(cn.tenant_id));
        strcpy(cn.type, "skill");
        strcpy(cn.subtype, "llm_config");
        strcpy(cn.name, "LLM Configuration");
        cn.is_active = true;
        cn.updated_at = (uint64_t)time(NULL);
        cn.content = cJSON_CreateObject();
        cJSON_AddStringToObject(cn.content, "provider", llm_type);
        if (llm_key[0]) cJSON_AddStringToObject(cn.content, "api_key", llm_key);
        
        char buf[256];
        if (nvs_mgr_get_str(NVS_KEY_LLM_API_URL, buf, sizeof(buf)) && strlen(buf) > 0) cJSON_AddStringToObject(cn.content, "base_url", buf);
        if (nvs_mgr_get_str(NVS_KEY_LLM_MODEL, buf, sizeof(buf)) && strlen(buf) > 0) cJSON_AddStringToObject(cn.content, "model", buf);
        
        neuron_cache_upsert_node(&cn);
        cJSON_Delete(cn.content);
        ESP_LOGI(TAG, "  -> LLM config added to sync");
    }

    /* 3. GPIO Config */
    int32_t sda, scl;
    if (nvs_mgr_get_i32("gpio_sda", &sda)) {
        nvs_mgr_get_i32("gpio_scl", &scl);
        cache_node_t cn = {0};
        strlcpy(cn.id, s_config.tenant_id, sizeof(cn.id));
        cn.id[0] = '3'; cn.id[1] = '3'; cn.id[2] = '3';
        strlcpy(cn.tenant_id, s_config.tenant_id, sizeof(cn.tenant_id));
        strcpy(cn.type, "device");
        strcpy(cn.subtype, "gpio_config");
        strcpy(cn.name, "Hardware Pins");
        cn.is_active = true;
        cn.updated_at = (uint64_t)time(NULL);
        cn.content = cJSON_CreateObject();
        cJSON_AddNumberToObject(cn.content, "sda", sda);
        cJSON_AddNumberToObject(cn.content, "scl", scl);
        
        int32_t val;
        if (nvs_mgr_get_i32("gpio_mic_sck", &val)) cJSON_AddNumberToObject(cn.content, "mic_sck", val);
        if (nvs_mgr_get_i32("gpio_mic_ws", &val)) cJSON_AddNumberToObject(cn.content, "mic_ws", val);
        if (nvs_mgr_get_i32("gpio_mic_sd", &val)) cJSON_AddNumberToObject(cn.content, "mic_sd", val);
        if (nvs_mgr_get_i32("gpio_spk_bclk", &val)) cJSON_AddNumberToObject(cn.content, "spk_bclk", val);
        if (nvs_mgr_get_i32("gpio_spk_lrc", &val)) cJSON_AddNumberToObject(cn.content, "spk_lrc", val);
        if (nvs_mgr_get_i32("gpio_spk_dout", &val)) cJSON_AddNumberToObject(cn.content, "spk_dout", val);
        
        neuron_cache_upsert_node(&cn);
        cJSON_Delete(cn.content);
        ESP_LOGI(TAG, "  -> GPIO config added to sync");
    }
#endif
}

static void neuron_link_apply_config(cJSON *node) {
    if (!node) return;
    cJSON *subtype = cJSON_GetObjectItem(node, "subtype");
    cJSON *content = cJSON_GetObjectItem(node, "content");
    if (!subtype || !content || !cJSON_IsString(subtype)) return;

    ESP_LOGI(TAG, "Applying config for subtype: %s", subtype->valuestring);

    if (strcmp(subtype->valuestring, "interface_telegram") == 0) {
        cJSON *token = cJSON_GetObjectItem(content, "bot_token");
        cJSON *chat = cJSON_GetObjectItem(content, "chat_id");
        bool changed = false;
        
        if (token && cJSON_IsString(token) && strlen(token->valuestring) > 0) {
            char current[128] = "";
            nvs_mgr_get_str(NVS_KEY_TG_TOKEN, current, sizeof(current));
            if (strcmp(current, token->valuestring) != 0) {
                char masked[16];
                size_t tk_len = strlen(token->valuestring);
                if (tk_len > 10) {
                    snprintf(masked, sizeof(masked), "%.5s...%.5s", token->valuestring, token->valuestring + tk_len - 5);
                } else {
                    strcpy(masked, "****");
                }
                ESP_LOGW(TAG, "New Telegram token detected: %s", masked);
                nvs_mgr_set_str(NVS_KEY_TG_TOKEN, token->valuestring);
                changed = true;
            }
        }
        
        if (chat && cJSON_IsString(chat) && strlen(chat->valuestring) > 0) {
            char current_chat[256] = "";
            nvs_mgr_get_str(NVS_KEY_TG_CHAT_IDS, current_chat, sizeof(current_chat));
            if (strcmp(current_chat, chat->valuestring) != 0) {
                ESP_LOGW(TAG, "New Telegram Chat IDs detected!");
                nvs_mgr_set_str(NVS_KEY_TG_CHAT_IDS, chat->valuestring);
                changed = true;
            }
        }

        if (changed) {
            ESP_LOGE(TAG, "Telegram configuration updated via Cloud Sync. REBOOTING in 3s...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        }
    } 
    else if (strcmp(subtype->valuestring, "llm_config") == 0) {
        cJSON *prov = cJSON_GetObjectItem(content, "provider");
        cJSON *key = cJSON_GetObjectItem(content, "api_key");
        cJSON *url = cJSON_GetObjectItem(content, "base_url");
        cJSON *model = cJSON_GetObjectItem(content, "model");
        bool changed = false;
        if (prov) { nvs_mgr_set_str(NVS_KEY_LLM_BACKEND, prov->valuestring); changed = true; }
        if (key) { nvs_mgr_set_str(NVS_KEY_LLM_API_KEY, key->valuestring); changed = true; }
        if (url) { nvs_mgr_set_str(NVS_KEY_LLM_API_URL, url->valuestring); changed = true; }
        if (model) { nvs_mgr_set_str(NVS_KEY_LLM_MODEL, model->valuestring); changed = true; }
        if (changed) {
            ESP_LOGW(TAG, "Reloading LLM provider registry with newly synced cloud config...");
            provider_registry_init();
        }
    }
    else if (strcmp(subtype->valuestring, "gpio_config") == 0) {
        const char *keys[] = {"sda", "scl", "mic_sck", "mic_ws", "mic_sd", "spk_bclk", "spk_lrc", "spk_dout"};
        const char *nvs_keys[] = {"gpio_sda", "gpio_scl", "gpio_mic_sck", "gpio_mic_ws", "gpio_mic_sd", "gpio_spk_bclk", "gpio_spk_lrc", "gpio_spk_dout"};
        bool changed = false;
        for (int i=0; i<8; i++) {
            cJSON *val = cJSON_GetObjectItem(content, keys[i]);
            if (val) {
                int32_t current;
                if (!nvs_mgr_get_i32(nvs_keys[i], &current) || current != (int32_t)val->valuedouble) {
                    nvs_mgr_set_i32(nvs_keys[i], (int32_t)val->valuedouble);
                    changed = true;
                }
            }
        }
        if (changed) {
            ESP_LOGW(TAG, "GPIO config changed via cloud. Rebooting in 3 seconds...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        }
    }
}

/* ============================================================
 * Sync Operations
 * ============================================================ */

esp_err_t neuron_link_sync(sync_mode_t mode, sync_direction_t direction, nl_sync_result_t *result) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    display_ui_show_message("CLOUD SYNC", "Syncing...", "Please wait");
    
    int64_t start_time = esp_timer_get_time() / 1000;
    
    memset(&s_last_result, 0, sizeof(s_last_result));
    s_last_result.status = NL_STATUS_SYNCING;
    s_status = NL_STATUS_SYNCING;
    
    ESP_LOGI(TAG, "Starting sync: mode=%d, direction=%d", mode, direction);
    
    /* Prepare local config nodes (Telegram, etc.) before pushing */
    neuron_link_prepare_local_config();
    
    /* Delta sync: push unsynced items to cloud */
    if (direction == SYNC_DIR_DEVICE_TO_CLOUD || direction == SYNC_DIR_BIDIRECTIONAL) {
        /* Push unsynced nodes */
        uint32_t node_count = 0;
        cache_node_t **nodes = neuron_cache_get_unsynced_nodes(&node_count);
        if (nodes) {
            for (uint32_t i = 0; i < node_count; i++) {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "id", nodes[i]->id);
                cJSON_AddStringToObject(json, "tenant_id", nodes[i]->tenant_id);
                cJSON_AddStringToObject(json, "type", nodes[i]->type);
                cJSON_AddStringToObject(json, "name", nodes[i]->name);
                if (nodes[i]->subtype[0]) cJSON_AddStringToObject(json, "subtype", nodes[i]->subtype);
                cJSON_AddNumberToObject(json, "pos_x", nodes[i]->pos_x);
                cJSON_AddNumberToObject(json, "pos_y", nodes[i]->pos_y);
                cJSON_AddNumberToObject(json, "pos_z", nodes[i]->pos_z);
                cJSON_AddBoolToObject(json, "is_active", nodes[i]->is_active);
                char updated_str[32] = "";
                time_t rawtime = (time_t)nodes[i]->updated_at;
                struct tm ts;
                gmtime_r(&rawtime, &ts);
                strftime(updated_str, sizeof(updated_str), "%Y-%m-%dT%H:%M:%SZ", &ts);
                cJSON_AddStringToObject(json, "updated_at", updated_str);
                if (nodes[i]->content) {
                    cJSON_AddItemToObject(json, "content", cJSON_Duplicate(nodes[i]->content, true));
                }
                
                char *body = cJSON_PrintUnformatted(json);
                cJSON_Delete(json);
                
                char endpoint[128];
                snprintf(endpoint, sizeof(endpoint), "/rest/v1/nodes");
                
                int status = 0;
                esp_err_t err = http_post(endpoint, body, NULL, &status);
                if (err == ESP_OK && status >= 200 && status < 300) {
                    neuron_cache_mark_node_synced(nodes[i]->id);
                    s_last_result.nodes_sent++;
                }
                
                free(body);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            neuron_cache_free_nodes(nodes, node_count);
        }
        
        /* Push unsynced links */
        uint32_t link_count = 0;
        cache_link_t **links = neuron_cache_get_unsynced_links(&link_count);
        if (links) {
            for (uint32_t i = 0; i < link_count; i++) {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "id", links[i]->id);
                cJSON_AddStringToObject(json, "tenant_id", links[i]->tenant_id);
                cJSON_AddStringToObject(json, "source_node_id", links[i]->source_id);
                cJSON_AddStringToObject(json, "target_node_id", links[i]->target_id);
                cJSON_AddStringToObject(json, "type", links[i]->type);
                cJSON_AddNumberToObject(json, "weight", links[i]->weight);
                char updated_str[32] = "";
                time_t rawtime = (time_t)links[i]->updated_at;
                struct tm ts;
                gmtime_r(&rawtime, &ts);
                strftime(updated_str, sizeof(updated_str), "%Y-%m-%dT%H:%M:%SZ", &ts);
                cJSON_AddStringToObject(json, "updated_at", updated_str);
                
                char *body = cJSON_PrintUnformatted(json);
                cJSON_Delete(json);
                
                int status = 0;
                esp_err_t err = http_post("/rest/v1/links", body, NULL, &status);
                if (err == ESP_OK && status >= 200 && status < 300) {
                    neuron_cache_mark_link_synced(links[i]->id);
                    s_last_result.links_sent++;
                }
                
                free(body);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            neuron_cache_free_links(links, link_count);
        }
        
        /* Push unsynced pulses */
        uint32_t pulse_count = 0;
        cache_pulse_t **pulses = neuron_cache_get_unsynced_pulses(&pulse_count);
        if (pulses) {
            for (uint32_t i = 0; i < pulse_count; i++) {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "id", pulses[i]->id);
                cJSON_AddStringToObject(json, "tenant_id", pulses[i]->tenant_id);
                cJSON_AddStringToObject(json, "device_id", pulses[i]->device_id);
                cJSON_AddStringToObject(json, "type", pulses[i]->type);
                if (pulses[i]->source_id[0]) cJSON_AddStringToObject(json, "source_node_id", pulses[i]->source_id);
                if (pulses[i]->target_id[0]) cJSON_AddStringToObject(json, "target_node_id", pulses[i]->target_id);
                if (pulses[i]->text) cJSON_AddStringToObject(json, "text", pulses[i]->text);
                cJSON_AddNumberToObject(json, "energy", pulses[i]->energy);
                cJSON_AddNumberToObject(json, "created_at", pulses[i]->created_at);
                
                char *body = cJSON_PrintUnformatted(json);
                cJSON_Delete(json);
                
                int status = 0;
                esp_err_t err = http_post("/rest/v1/pulses", body, NULL, &status);
                if (err == ESP_OK && status >= 200 && status < 300) {
                    neuron_cache_mark_pulse_synced(pulses[i]->id);
                    s_last_result.pulses_sent++;
                }
                
                free(body);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            neuron_cache_free_pulses(pulses, pulse_count);
        }
    }
    
    /* Cloud to device sync */
    if (direction == SYNC_DIR_CLOUD_TO_DEVICE || direction == SYNC_DIR_BIDIRECTIONAL) {
        uint64_t since = (mode == SYNC_MODE_DELTA) ? neuron_cache_get_last_sync() : 0;
        
        /* Pull nodes */
        char *nodes_json = neuron_link_pull_nodes(since);
        if (nodes_json) {
            ESP_LOGI(TAG, "Pulled nodes JSON (%d bytes)", (int)strlen(nodes_json));
            cJSON *nodes = cJSON_Parse(nodes_json);
            if (nodes && cJSON_IsArray(nodes)) {
                cJSON *node = NULL;
                cJSON_ArrayForEach(node, nodes) {
                    cache_node_t cn = {0};
                    cJSON *id = cJSON_GetObjectItem(node, "id");
                    cJSON *tenant = cJSON_GetObjectItem(node, "tenant_id");
                    cJSON *type = cJSON_GetObjectItem(node, "type");
                    cJSON *name = cJSON_GetObjectItem(node, "name");
                    cJSON *subtype = cJSON_GetObjectItem(node, "subtype");
                    cJSON *pos_x = cJSON_GetObjectItem(node, "pos_x");
                    cJSON *pos_y = cJSON_GetObjectItem(node, "pos_y");
                    cJSON *pos_z = cJSON_GetObjectItem(node, "pos_z");
                    cJSON *active = cJSON_GetObjectItem(node, "is_active");
                    cJSON *updated = cJSON_GetObjectItem(node, "updated_at");
                    
                    if (id && cJSON_IsString(id)) strncpy(cn.id, id->valuestring, 36);
                    if (tenant && cJSON_IsString(tenant)) strncpy(cn.tenant_id, tenant->valuestring, 36);
                    if (type && cJSON_IsString(type)) strncpy(cn.type, type->valuestring, 31);
                    if (name && cJSON_IsString(name)) strncpy(cn.name, name->valuestring, 127);
                    if (subtype && cJSON_IsString(subtype)) strncpy(cn.subtype, subtype->valuestring, 63);
                    if (pos_x) cn.pos_x = pos_x->valuedouble;
                    if (pos_y) cn.pos_y = pos_y->valuedouble;
                    if (pos_z) cn.pos_z = pos_z->valuedouble;
                    if (active) cn.is_active = cJSON_IsTrue(active);
                    if (updated) cn.updated_at = updated->valueint;
                    cn.synced = true;
                    
                    /* If it's a config node, apply it to system NVS */
                    neuron_link_apply_config(node);
                    
                    neuron_cache_upsert_node(&cn);
                    s_last_result.nodes_received++;
                    vTaskDelay(pdMS_TO_TICKS(5)); /* Yield during heavy processing */
                }
            }
            cJSON_Delete(nodes);
            free(nodes_json);
        }
        
        /* Pull links */
        char *links_json = neuron_link_pull_links(since);
        if (links_json) {
            cJSON *links = cJSON_Parse(links_json);
            if (links && cJSON_IsArray(links)) {
                cJSON *link = NULL;
                cJSON_ArrayForEach(link, links) {
                    cache_link_t cl = {0};
                    cJSON *id = cJSON_GetObjectItem(link, "id");
                    cJSON *tenant = cJSON_GetObjectItem(link, "tenant_id");
                    cJSON *source = cJSON_GetObjectItem(link, "source_node_id");
                    cJSON *target = cJSON_GetObjectItem(link, "target_node_id");
                    cJSON *type = cJSON_GetObjectItem(link, "type");
                    cJSON *weight = cJSON_GetObjectItem(link, "weight");
                    cJSON *updated = cJSON_GetObjectItem(link, "updated_at");
                    
                    if (id && cJSON_IsString(id)) strncpy(cl.id, id->valuestring, 36);
                    if (tenant && cJSON_IsString(tenant)) strncpy(cl.tenant_id, tenant->valuestring, 36);
                    if (source && cJSON_IsString(source)) strncpy(cl.source_id, source->valuestring, 36);
                    if (target && cJSON_IsString(target)) strncpy(cl.target_id, target->valuestring, 36);
                    if (type && cJSON_IsString(type)) strncpy(cl.type, type->valuestring, 31);
                    if (weight) cl.weight = weight->valuedouble;
                    if (updated) cl.updated_at = updated->valueint;
                    cl.synced = true;
                    
                    neuron_cache_upsert_link(&cl);
                    s_last_result.links_received++;
                }
            }
            cJSON_Delete(links);
            free(links_json);
        }
    }
    
    /* Pull workflows from cloud → workflow engine */
    neuron_link_sync_workflows();

    /* Update last sync time */
    uint64_t now = (uint64_t)time(NULL);
    neuron_cache_set_last_sync(now);
    
    /* Complete */
    s_last_result.status = NL_STATUS_COMPLETE;
    s_last_result.duration_ms = (uint32_t)(esp_timer_get_time() / 1000 - start_time);
    s_status = NL_STATUS_COMPLETE;
    
    if (result) memcpy(result, &s_last_result, sizeof(s_last_result));
    
    ESP_LOGI(TAG, "Sync complete: nodes %" PRIu32 "/%" PRIu32 ", links %" PRIu32 "/%" PRIu32 ", pulses %" PRIu32 "/%" PRIu32 ", %" PRIu64 "ms",
             s_last_result.nodes_sent, s_last_result.nodes_received,
             s_last_result.links_sent, s_last_result.links_received,
             s_last_result.pulses_sent, s_last_result.pulses_received,
             s_last_result.duration_ms);
    
    char stats[32];
    snprintf(stats, sizeof(stats), "N:%d L:%d", (int)s_last_result.nodes_received, (int)s_last_result.links_received);
    display_ui_show_message("SYNC COMPLETE", "Success!", stats);
    vTaskDelay(pdMS_TO_TICKS(2000));
    display_ui_set_state(DISPLAY_STATE_READY);

    return ESP_OK;
}

esp_err_t neuron_link_delta_sync(void) {
    return neuron_link_sync(SYNC_MODE_DELTA, SYNC_DIR_DEVICE_TO_CLOUD, NULL);
}

esp_err_t neuron_link_full_sync(void) {
    nl_sync_result_t result;
    return neuron_link_sync(SYNC_MODE_FULL, SYNC_DIR_BIDIRECTIONAL, &result);
}

const nl_sync_result_t* neuron_link_get_last_result(void) {
    return &s_last_result;
}

uint64_t neuron_link_get_last_sync_time(void) {
    return neuron_cache_get_last_sync();
}

/* ============================================================
 * Node Operations
 * ============================================================ */

esp_err_t neuron_link_push_node(const char *node_json) {
    if (!node_json) return ESP_ERR_INVALID_ARG;
    
    int status = 0;
    esp_err_t err = http_post("/rest/v1/nodes", node_json, NULL, &status);
    
    if (err == ESP_OK && status >= 200 && status < 300) {
        return ESP_OK;
    }
    
    return ESP_FAIL;
}

char* neuron_link_pull_nodes(uint64_t since) {
    if (!s_initialized || !s_config.tenant_id[0]) return NULL;

    char endpoint[256];
    if (since > 0) {
        char since_str[32] = "";
        time_t rawtime = (time_t)since;
        struct tm ts;
        gmtime_r(&rawtime, &ts);
        strftime(since_str, sizeof(since_str), "%Y-%m-%dT%H:%M:%SZ", &ts);

        snprintf(endpoint, sizeof(endpoint), 
                 "/rest/v1/nodes?tenant_id=eq.%s&updated_at=gt.%s&select=*", 
                 s_config.tenant_id, since_str);
    } else {
        snprintf(endpoint, sizeof(endpoint), "/rest/v1/nodes?tenant_id=eq.%s&select=*", s_config.tenant_id);
    }
    
    char *response = NULL;
    int status = 0;
    esp_err_t err = http_get(endpoint, &response, &status);
    
    if (err == ESP_OK && status == 200) {
        return response;
    }
    
    if (response) free(response);
    return NULL;
}

esp_err_t neuron_link_delete_node(const char *node_id) {
    if (!node_id) return ESP_ERR_INVALID_ARG;
    
    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint), "/rest/v1/nodes?id=eq.%s", node_id);
    
    int status = 0;
    return http_delete(endpoint, &status);
}

/* ============================================================
 * Link Operations
 * ============================================================ */

esp_err_t neuron_link_push_link(const char *link_json) {
    if (!link_json) return ESP_ERR_INVALID_ARG;
    
    int status = 0;
    esp_err_t err = http_post("/rest/v1/links", link_json, NULL, &status);
    
    return (err == ESP_OK && status >= 200 && status < 300) ? ESP_OK : ESP_FAIL;
}

char* neuron_link_pull_links(uint64_t since) {
    if (!s_initialized || !s_config.tenant_id[0]) return NULL;

    char endpoint[256];
    if (since > 0) {
        char since_str[32] = "";
        time_t rawtime = (time_t)since;
        struct tm ts;
        gmtime_r(&rawtime, &ts);
        strftime(since_str, sizeof(since_str), "%Y-%m-%dT%H:%M:%SZ", &ts);

        snprintf(endpoint, sizeof(endpoint), 
                 "/rest/v1/links?tenant_id=eq.%s&updated_at=gt.%s&select=*", 
                 s_config.tenant_id, since_str);
    } else {
        snprintf(endpoint, sizeof(endpoint), "/rest/v1/links?tenant_id=eq.%s&select=*", s_config.tenant_id);
    }
    
    char *response = NULL;
    int status = 0;
    esp_err_t err = http_get(endpoint, &response, &status);
    
    if (err == ESP_OK && status == 200) {
        return response;
    }
    
    if (response) free(response);
    return NULL;
}

esp_err_t neuron_link_delete_link(const char *link_id) {
    if (!link_id) return ESP_ERR_INVALID_ARG;
    
    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint), "/rest/v1/links?id=eq.%s", link_id);
    
    int status = 0;
    return http_delete(endpoint, &status);
}

/* ============================================================
 * Pulse Operations
 * ============================================================ */

esp_err_t neuron_link_push_pulse(const char *pulse_json) {
    if (!pulse_json) return ESP_ERR_INVALID_ARG;
    
    int status = 0;
    return http_post("/rest/v1/pulses", pulse_json, NULL, &status);
}

char* neuron_link_pull_pulses(const char *node_id, uint32_t limit) {
    char endpoint[256];
    if (node_id) {
        snprintf(endpoint, sizeof(endpoint),
                 "/rest/v1/pulses?or=(source_node_id.eq.%s,target_node_id.eq.%s)&limit=%" PRIu32 "&order=created_at.desc",
                 node_id, node_id, limit);
    } else {
        snprintf(endpoint, sizeof(endpoint),
                 "/rest/v1/pulses?limit=%" PRIu32 "&order=created_at.desc", limit);
    }
    
    char *response = NULL;
    http_get(endpoint, &response, NULL);
    
    return response;
}

/* ============================================================
 * Workflow Sync (Cloud → Device)
 * ============================================================ */

/**
 * Pull workflows from cloud and load into workflow engine.
 * Called during full sync to bootstrap or update workflow definitions.
 */
esp_err_t neuron_link_sync_workflows(void) {
    if (!s_initialized || !s_config.tenant_id[0]) {
        ESP_LOGW(TAG, "Sync workflows skipped: No tenant ID (unpaired)");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Syncing workflows from cloud...");

    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint),
             "/rest/v1/workflows?tenant_id=eq.%s&is_enabled=eq.true&select=id,name,icon,description,category,"
             "trigger_type,trigger_pattern,trigger_node_id,priority,steps,is_enabled,on_error,max_retries",
             s_config.tenant_id);

    char *response = NULL;
    int status = 0;
    esp_err_t err = http_get(endpoint, &response, &status);

    if (err != ESP_OK || status != 200 || !response) {
        ESP_LOGW(TAG, "Failed to fetch workflows: err=%d, status=%d", err, status);
        if (response) free(response);
        return err;
    }

    ESP_LOGI(TAG, "Got workflows response (%d bytes)", (int)strlen(response));

    /* Load into workflow engine */

    workflow_engine_abort(); /* Abort any running workflow first */
    err = workflow_engine_load(response, strlen(response));

    free(response);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Workflows loaded: %d workflows", workflow_engine_count());
    }

    return err;
}

/**
 * Pull workflows on demand (e.g., from agent tool).
 */
esp_err_t neuron_link_pull_workflows(void) {
    return neuron_link_sync_workflows();
}

/* ============================================================
 * Selective Config Sync (Cloud → Device)
 * Only applies cloud config if local NVS is empty.
 * Called on explicit user request (not on auto-sync).
 * ============================================================ */

/**
 * Pull only config nodes (Telegram, LLM, GPIO) from cloud and apply to NVS.
 * Only overwrites NVS if the local key is empty — preserves existing local config.
 * @return ESP_OK on success
 */
esp_err_t neuron_link_sync_config_only(void) {
    if (!s_initialized || !s_config.tenant_id[0]) {
        ESP_LOGW(TAG, "Config sync skipped: not initialized or not paired");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Selective config sync: pulling from cloud...");

    /* Pull config nodes - broadening search to any node in the tenant */
    char endpoint[384];
    snprintf(endpoint, sizeof(endpoint),
             "/rest/v1/nodes?tenant_id=eq.%s&select=*",
             s_config.tenant_id);

    char *response = NULL;
    int status = 0;
    esp_err_t err = http_get(endpoint, &response, &status);

    if (err != ESP_OK || status != 200 || !response) {
        ESP_LOGW(TAG, "Failed to fetch config nodes: err=%d, status=%d", err, status);
        if (response) free(response);
        return err;
    }

    cJSON *nodes = cJSON_Parse(response);
    free(response);

    if (!nodes || !cJSON_IsArray(nodes) || cJSON_GetArraySize(nodes) == 0) {
        ESP_LOGW(TAG, "No config nodes found in cloud for tenant: %s", s_config.tenant_id);
        if (response) ESP_LOGD(TAG, "Raw response: %s", response);
        if (nodes) cJSON_Delete(nodes);
        return ESP_FAIL;
    }

    int applied = 0;
    cJSON *node = NULL;
    cJSON_ArrayForEach(node, nodes) {
        cJSON *subtype = cJSON_GetObjectItem(node, "subtype");
        if (!subtype || !cJSON_IsString(subtype)) continue;

        ESP_LOGI(TAG, "Checking config: subtype=%s", subtype->valuestring);

        /* Telegram config */
        if (strcmp(subtype->valuestring, "interface_telegram") == 0) {
            cJSON *content = cJSON_GetObjectItem(node, "content");
            if (!content) continue;

            cJSON *token = cJSON_GetObjectItem(content, "bot_token");
            cJSON *chat = cJSON_GetObjectItem(content, "chat_id");

            char current_token[128] = {0};
            nvs_mgr_get_str(NVS_KEY_TG_TOKEN, current_token, sizeof(current_token));

            /* Only apply if local token is empty */
            if (token && cJSON_IsString(token) && strlen(token->valuestring) > 0 && strlen(current_token) == 0) {
                ESP_LOGI(TAG, "Applying Telegram token from cloud (local was empty)...");
                nvs_mgr_set_str(NVS_KEY_TG_TOKEN, token->valuestring);
                applied++;
            }

            char current_chat[256] = {0};
            nvs_mgr_get_str(NVS_KEY_TG_CHAT_IDS, current_chat, sizeof(current_chat));

            if (chat && cJSON_IsString(chat) && strlen(chat->valuestring) > 0 && strlen(current_chat) == 0) {
                ESP_LOGI(TAG, "Applying Telegram chat IDs from cloud (local was empty)...");
                nvs_mgr_set_str(NVS_KEY_TG_CHAT_IDS, chat->valuestring);
                applied++;
            }
        }

        /* LLM config */
        if (strcmp(subtype->valuestring, "llm_config") == 0) {
            cJSON *content = cJSON_GetObjectItem(node, "content");
            if (!content) continue;

            cJSON *prov = cJSON_GetObjectItem(content, "provider");
            cJSON *key = cJSON_GetObjectItem(content, "api_key");
            cJSON *url = cJSON_GetObjectItem(content, "base_url");
            cJSON *model = cJSON_GetObjectItem(content, "model");

            char current[256] = {0};

            if (prov && cJSON_IsString(prov)) {
                nvs_mgr_get_str(NVS_KEY_LLM_BACKEND, current, sizeof(current));
                if (strlen(current) == 0 || strcmp(current, prov->valuestring) != 0) {
                    nvs_mgr_set_str(NVS_KEY_LLM_BACKEND, prov->valuestring);
                    applied++;
                }
            }
            if (key && cJSON_IsString(key)) {
                nvs_mgr_get_str(NVS_KEY_LLM_API_KEY, current, sizeof(current));
                if (strlen(current) == 0 || strcmp(current, key->valuestring) != 0) {
                    nvs_mgr_set_str(NVS_KEY_LLM_API_KEY, key->valuestring);
                    applied++;
                }
            }
            if (url && cJSON_IsString(url)) {
                nvs_mgr_get_str(NVS_KEY_LLM_API_URL, current, sizeof(current));
                if (strlen(current) == 0 || strcmp(current, url->valuestring) != 0) {
                    nvs_mgr_set_str(NVS_KEY_LLM_API_URL, url->valuestring);
                    applied++;
                }
            }
            if (model && cJSON_IsString(model)) {
                nvs_mgr_get_str(NVS_KEY_LLM_MODEL, current, sizeof(current));
                if (strlen(current) == 0 || strcmp(current, model->valuestring) != 0) {
                    nvs_mgr_set_str(NVS_KEY_LLM_MODEL, model->valuestring);
                    applied++;
                }
            }
        }

        /* GPIO config */
        if (strcmp(subtype->valuestring, "gpio_config") == 0) {
            cJSON *content = cJSON_GetObjectItem(node, "content");
            if (!content) continue;

            const char *keys[] = {"sda", "scl", "mic_sck", "mic_ws", "mic_sd", "spk_bclk", "spk_lrc", "spk_dout"};
            const char *nvs_keys[] = {"gpio_sda", "gpio_scl", "gpio_mic_sck", "gpio_mic_ws", "gpio_mic_sd", "gpio_spk_bclk", "gpio_spk_lrc", "gpio_spk_dout"};

            for (int i = 0; i < 8; i++) {
                cJSON *val = cJSON_GetObjectItem(content, keys[i]);
                if (!val) continue;

                int32_t current;
                if (!nvs_mgr_get_i32(nvs_keys[i], &current) || current != (int32_t)val->valuedouble) {
                    nvs_mgr_set_i32(nvs_keys[i], (int32_t)val->valuedouble);
                    applied++;
                    ESP_LOGI(TAG, "Applied GPIO %s from cloud", keys[i]);
                }
            }
        }
    }

    cJSON_Delete(nodes);

    ESP_LOGI(TAG, "Config sync done: %d keys applied from cloud", applied);

    if (applied > 0) {
        ESP_LOGW(TAG, "Config changed from cloud. Rebooting in 3s...");
        display_ui_show_message("CONFIG UPDATED", "New settings applied", "Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    } else {
        display_ui_show_message("CONFIG SYNC", "Already configured", "No changes needed");
        vTaskDelay(pdMS_TO_TICKS(1500));
        display_ui_set_state(DISPLAY_STATE_READY);
    }

    return ESP_OK;
}

/* ============================================================
 * Real-time Updates
 * ============================================================ */

static void (*s_node_callback)(const char *) = NULL;
static void (*s_link_callback)(const char *) = NULL;
static void (*s_pulse_callback)(const char *) = NULL;

static void __attribute__((unused)) ws_event_handler(void *handler_args, esp_event_base_t base, 
                             int32_t event_id, void *event_data) {
    /* Stub - WebSocket not implemented in ESP-IDF v5.3 */
    (void)handler_args;
    (void)base;
    (void)event_id;
    (void)event_data;
    ESP_LOGW(TAG, "WebSocket event handler called but WebSocket is stubbed");
}

esp_err_t neuron_link_subscribe_nodes(void (*callback)(const char *)) {
    s_node_callback = callback;
    
    if (!s_ws_connected) {
        /* TODO: Connect WebSocket */
    }
    
    return ESP_OK;
}

esp_err_t neuron_link_subscribe_links(void (*callback)(const char *)) {
    s_link_callback = callback;
    return ESP_OK;
}

esp_err_t neuron_link_subscribe_pulses(void (*callback)(const char *)) {
    s_pulse_callback = callback;
    return ESP_OK;
}

esp_err_t neuron_link_unsubscribe_all(void) {
    s_node_callback = NULL;
    s_link_callback = NULL;
    s_pulse_callback = NULL;
    
    return ESP_OK;
}

/* ============================================================
 * Status & Monitoring
 * ============================================================ */

nl_status_t neuron_link_get_status(void) {
    return s_status;
}

void neuron_link_get_connection_info(char *buf, size_t buf_len) {
    if (!buf) return;
    
    snprintf(buf, buf_len,
             "Neuron Link: %s\n"
             "  Status: %s\n"
             "  Connected: %s\n"
             "  Last sync: %lu ago\n"
             "  Pending: %" PRIu32,
             s_config.supabase_url,
             s_status == NL_STATUS_IDLE ? "IDLE" :
             s_status == NL_STATUS_CONNECTING ? "CONNECTING" :
             s_status == NL_STATUS_SYNCING ? "SYNCING" :
             s_status == NL_STATUS_COMPLETE ? "COMPLETE" : "ERROR",
             s_ws_connected ? "YES" : "NO",
             (unsigned long)(time(NULL) - neuron_cache_get_last_sync()),
             s_last_result.nodes_sent + s_last_result.links_sent + s_last_result.pulses_sent);
}

/* ============================================================
 * Device Pairing
 * ============================================================ */

esp_err_t neuron_link_generate_pairing_code(char *out_code) {
    if (!out_code) return ESP_ERR_INVALID_ARG;
    
    /* Generate 6-digit code */
    srand(esp_timer_get_time());
    int code = 100000 + (rand() % 900000);
    snprintf(out_code, 8, "%06d", code);
    
    /* Store code in NVS with expiry */
    nvs_mgr_set_str_ns("pairing", "code", out_code);
    nvs_mgr_set_str_ns("pairing", "expires", "temp");  /* Simplified - use timestamp */
    
    ESP_LOGI(TAG, "Pairing code: %s", out_code);
    
    return ESP_OK;
}

bool neuron_link_is_paired(void) {
    char tenant_id[64] = {0};
    nvs_mgr_get_str("tenant_id", tenant_id, sizeof(tenant_id));
    return strlen(tenant_id) > 0;
}

esp_err_t neuron_link_get_tenant_info(char *tenant_id, char *tenant_name) {
    if (!tenant_id || !tenant_name) return ESP_ERR_INVALID_ARG;
    
    nvs_mgr_get_str("tenant_id", tenant_id, 64);
    nvs_mgr_get_str("tenant_name", tenant_name, 128);
    
    return ESP_OK;
}

esp_err_t neuron_link_unpair(void) {
    nvs_mgr_erase_key("device", "tenant_id");
    nvs_mgr_erase_key("device", "tenant_name");
    nvs_mgr_erase_key("device", "device_key");
    
    neuron_cache_reset();
    
    ESP_LOGI(TAG, "Device unpaired");
    
    return ESP_OK;
}
