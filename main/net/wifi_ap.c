/*
 * ESPClaw - net/wifi_ap.c
 * Premium Config Portal with detailed hardware and service settings
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wifi_ap.h"
#include "wifi_manager.h"
#include "mem/nvs_manager.h"
#include "nvs_keys.h"
#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "mdns.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "wifi_ap";

#define AP_PASSWORD "espclaw1"
#define AP_CHANNEL 1

static bool s_ap_active = false;
static bool s_httpd_active = false;
static espclaw_wifi_ap_config_t s_config = {0};
static httpd_handle_t s_httpd = NULL;
static TaskHandle_t s_dns_task = NULL;
static volatile bool s_run_dns = false;

/* ============================================================
 * HTML Templates (Multi-section Config)
 * ============================================================ */

static const char s_html_header[] = 
    "<!DOCTYPE html><html lang=\"en\"><head>"
    "<meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>ESPClaw | Dashboard</title>"
    "<link href=\"https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600&display=swap\" rel=\"stylesheet\">"
    "<style>"
    ":root{--primary:#6366f1;--success:#22c55e;--bg:#0f172a;--card:rgba(30,41,59,0.7);}"
    "*{box-sizing:border-box;margin:0;padding:0;font-family:'Outfit',sans-serif;}"
    "body{background:var(--bg);color:#f8fafc;padding:20px;display:flex;flex-direction:column;align-items:center;}"
    ".container{width:100%%;max-width:600px;animation:fadeIn 0.6s ease-out;}"
    "@keyframes fadeIn{from{opacity:0;transform:translateY(10px);}to{opacity:1;transform:translateY(0);}}"
    ".card{background:var(--card);backdrop-filter:blur(12px);border:1px solid rgba(255,255,255,0.1);border-radius:20px;padding:30px;margin-bottom:20px;box-shadow:0 10px 30px rgba(0,0,0,0.3);}"
    "h1{font-size:24px;margin-bottom:20px;background:linear-gradient(to right, #818cf8, #c084fc);-webkit-background-clip:text;-webkit-text-fill-color:transparent;text-align:center;}"
    "h2{font-size:16px;color:#818cf8;text-transform:uppercase;letter-spacing:1px;margin-bottom:20px;display:flex;align-items:center;}"
    "h2::after{content:'';flex:1;height:1px;background:rgba(129,140,248,0.2);margin-left:15px;}"
    ".group{margin-bottom:15px;}"
    "label{display:block;font-size:13px;color:#94a3b8;margin-bottom:6px;}"
    "input, select{width:100%%;padding:12px;background:rgba(15,23,42,0.6);border:1px solid rgba(255,255,255,0.1);border-radius:10px;color:#fff;font-size:14px;}"
    "input:focus{outline:none;border-color:var(--primary);}"
    ".btn{width:100%%;padding:12px;border:none;border-radius:10px;font-size:14px;font-weight:600;cursor:pointer;transition:all 0.2s;margin-top:10px;color:#fff;}"
    ".btn-primary{background:var(--primary);box-shadow:0 4px 14px rgba(99,102,241,0.4);}"
    ".btn-success{background:var(--success);box-shadow:0 4px 14px rgba(34,197,94,0.4);}"
    ".btn:hover{transform:translateY(-2px);filter:brightness(1.1);}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:15px;}"
    "</style></head><body>"
    "<div class=\"container\"><h1>ESPClaw Configuration</h1>";

static const char s_section_wifi[] = 
    "<div class=\"card\">"
    "<h2>WiFi Connection</h2>"
    "<form action=\"/save?type=wifi\" method=\"POST\">"
    "<div class=\"group\"><label>SSID</label><input type=\"text\" name=\"wifi_ssid\" value=\"%s\"></div>"
    "<div class=\"group\"><label>Password</label><input type=\"password\" name=\"wifi_password\" value=\"%s\"></div>"
    "<button type=\"submit\" class=\"btn btn-success\">Connect & Save</button>"
    "</form></div>";

static const char s_section_telegram[] = 
    "<div class=\"card\">"
    "<h2>Telegram Bot</h2>"
    "<form action=\"/save?type=tg\" method=\"POST\">"
    "<div class=\"group\"><label>Bot Token</label><input type=\"text\" name=\"tg_token\" value=\"%s\"></div>"
    "<div class=\"group\"><label>Your User ID</label><input type=\"text\" name=\"tg_userid\" value=\"%s\"></div>"
    "<button type=\"submit\" class=\"btn btn-primary\">Save Telegram Settings</button>"
    "</form></div>";

static const char s_section_llm[] = 
    "<div class=\"card\">"
    "<h2>LLM Provider</h2>"
    "<form action=\"/save?type=llm\" method=\"POST\">"
    "<div class=\"group\"><label>Provider</label><select name=\"llm_type\">"
    "<option value=\"anthropic\" %s>Anthropic (Claude)</option>"
    "<option value=\"openai\" %s>OpenAI (GPT)</option>"
    "<option value=\"custom\" %s>Custom OpenAI Compatible</option>"
    "</select></div>"
    "<div class=\"group\"><label>API Key</label><input type=\"password\" name=\"llm_api_key\" value=\"%s\"></div>"
    "<div class=\"group\"><label>Base URL</label><input type=\"text\" name=\"llm_base_url\" value=\"%s\"></div>"
    "<div class=\"group\"><label>Model Name</label><input type=\"text\" name=\"llm_model\" value=\"%s\"></div>"
    "<button type=\"submit\" class=\"btn btn-primary\">Save LLM Settings</button>"
    "</form></div>";


static const char s_section_gpio[] = 
    "<div class=\"card\">"
    "<h2>Hardware Pins (GPIO)</h2>"
    "<form action=\"/save?type=gpio\" method=\"POST\">"
    "<div class=\"grid\">"
    "<div class=\"group\"><label>OLED SDA</label><input type=\"number\" name=\"gpio_sda\" value=\"%d\"></div>"
    "<div class=\"group\"><label>OLED SCL</label><input type=\"number\" name=\"gpio_scl\" value=\"%d\"></div>"
    "<div class=\"group\"><label>Mic SCK</label><input type=\"number\" name=\"gpio_mic_sck\" value=\"%d\"></div>"
    "<div class=\"group\"><label>Mic WS</label><input type=\"number\" name=\"gpio_mic_ws\" value=\"%d\"></div>"
    "<div class=\"group\"><label>Mic SD</label><input type=\"number\" name=\"gpio_mic_sd\" value=\"%d\"></div>"
    "<div class=\"group\"><label>Speaker BCLK</label><input type=\"number\" name=\"gpio_spk_bclk\" value=\"%d\"></div>"
    "<div class=\"group\"><label>Speaker LRC</label><input type=\"number\" name=\"gpio_spk_lrc\" value=\"%d\"></div>"
    "<div class=\"group\"><label>Speaker DOUT</label><input type=\"number\" name=\"gpio_spk_dout\" value=\"%d\"></div>"
    "</div>"
    "<button type=\"submit\" class=\"btn btn-primary\">Save GPIO Settings</button>"
    "</form></div>";

static const char s_html_footer[] = 
    "<div style=\"text-align:center;color:#64748b;font-size:12px;margin-bottom:40px;\">ESPClaw v%s &bull; Running on ESP32-S3</div>"
    "</div></body></html>";

/* ============================================================
 * Memory Allocation
 * ============================================================ */

static void* malloc_ext(size_t size) {
#if CONFIG_SPIRAM
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

/* ============================================================
 * Forward declarations for JSON API handlers
 * ============================================================ */
static esp_err_t api_telegram_get_handler(httpd_req_t *req);
static esp_err_t api_telegram_post_handler(httpd_req_t *req);
static esp_err_t api_llm_get_handler(httpd_req_t *req);
static esp_err_t api_llm_post_handler(httpd_req_t *req);
static esp_err_t api_status_handler(httpd_req_t *req);
static esp_err_t api_restart_handler(httpd_req_t *req);

/* ============================================================
 * HTTP Handlers
 * ============================================================ */
static void wifi_ap_load_config_internal(void) {
    memset(&s_config, 0, sizeof(s_config));
    nvs_mgr_get_str(NVS_KEY_WIFI_SSID, s_config.wifi_ssid, 32);
    nvs_mgr_get_str(NVS_KEY_WIFI_PASS, s_config.wifi_password, 64);
    nvs_mgr_get_str(NVS_KEY_TG_TOKEN, s_config.tg_token, 127);
    nvs_mgr_get_str(NVS_KEY_TG_CHAT_IDS, s_config.tg_userid, 31);
    nvs_mgr_get_str(NVS_KEY_TENANT_ID, s_config.tenant_id, 39);
    if (!nvs_mgr_get_str(NVS_KEY_LLM_BACKEND, s_config.llm_type, 15)) strcpy(s_config.llm_type, "anthropic");
    nvs_mgr_get_str(NVS_KEY_LLM_API_KEY, s_config.llm_api_key, 255);
    nvs_mgr_get_str(NVS_KEY_LLM_API_URL, s_config.llm_base_url, 255);
    nvs_mgr_get_str(NVS_KEY_LLM_MODEL, s_config.llm_model, 63);
    
    int32_t val;
    if (nvs_mgr_get_i32("gpio_sda", &val)) s_config.gpio_sda = val; else s_config.gpio_sda = 1;
    if (nvs_mgr_get_i32("gpio_scl", &val)) s_config.gpio_scl = val; else s_config.gpio_scl = 2;
    if (nvs_mgr_get_i32("gpio_mic_sck", &val)) s_config.gpio_mic_sck = val; else s_config.gpio_mic_sck = 41;
    if (nvs_mgr_get_i32("gpio_mic_ws", &val)) s_config.gpio_mic_ws = val; else s_config.gpio_mic_ws = 42;
    if (nvs_mgr_get_i32("gpio_mic_sd", &val)) s_config.gpio_mic_sd = val; else s_config.gpio_mic_sd = 40;
    if (nvs_mgr_get_i32("gpio_spk_bclk", &val)) s_config.gpio_spk_bclk = val; else s_config.gpio_spk_bclk = 4;
    if (nvs_mgr_get_i32("gpio_spk_lrc", &val)) s_config.gpio_spk_lrc = val; else s_config.gpio_spk_lrc = 5;
    if (nvs_mgr_get_i32("gpio_spk_dout", &val)) s_config.gpio_spk_dout = val; else s_config.gpio_spk_dout = 6;
}


static esp_err_t root_handler(httpd_req_t *req) {
    /* Load latest config from NVS before rendering the page */
    wifi_ap_load_config_internal();
    
    char *buf = malloc_ext(16384);
    if (!buf) return ESP_ERR_NO_MEM;

    size_t len = snprintf(buf, 16384, s_html_header);
    len += snprintf(buf + len, 16384 - len, s_section_wifi, s_config.wifi_ssid, s_config.wifi_password);
    len += snprintf(buf + len, 16384 - len, s_section_telegram, s_config.tg_token, s_config.tg_userid);

    
    len += snprintf(buf + len, 16384 - len, s_section_llm, 
                   strcmp(s_config.llm_type, "anthropic") == 0 ? "selected" : "",
                   strcmp(s_config.llm_type, "openai") == 0 ? "selected" : "",
                   strcmp(s_config.llm_type, "custom") == 0 ? "selected" : "",
                   s_config.llm_api_key, s_config.llm_base_url, s_config.llm_model);
    
    len += snprintf(buf + len, 16384 - len, s_section_gpio,
                   s_config.gpio_sda, s_config.gpio_scl,
                   s_config.gpio_mic_sck, s_config.gpio_mic_ws, s_config.gpio_mic_sd,
                   s_config.gpio_spk_bclk, s_config.gpio_spk_lrc, s_config.gpio_spk_dout);
    
    len += snprintf(buf + len, 16384 - len, s_html_footer, ESPCLAW_VERSION);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, len);
    free(buf);
    return ESP_OK;
}


static esp_err_t save_handler(httpd_req_t *req) {
    /* FIRST: Load current config from NVS to ensure we don't wipe unrelated fields */
    wifi_ap_load_config_internal();
    
    char buf[2048] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    char *p = buf;
    while (p && *p) {
        char *key = p;
        char *val = strchr(p, '=');
        if (!val) break;
        *val++ = '\0';
        char *next = strchr(val, '&');
        if (next) *next++ = '\0';

        /* URL Decode */
        char *src = val, *dst = val;
        while (*src) {
            if (*src == '%') {
                int h; sscanf(src + 1, "%02x", &h); *dst++ = h; src += 3;
            } else if (*src == '+') {
                *dst++ = ' '; src++;
            } else {
                *dst++ = *src++;
            }
        }
        *dst = '\0';

        if (strcmp(key, "wifi_ssid") == 0) strncpy(s_config.wifi_ssid, val, 32);
        else if (strcmp(key, "wifi_password") == 0) strncpy(s_config.wifi_password, val, 64);
        else if (strcmp(key, "tg_token") == 0) strncpy(s_config.tg_token, val, 127);
        else if (strcmp(key, "tg_userid") == 0) strncpy(s_config.tg_userid, val, 31);
        else if (strcmp(key, "llm_type") == 0) strncpy(s_config.llm_type, val, 15);
        else if (strcmp(key, "llm_api_key") == 0) strncpy(s_config.llm_api_key, val, 255);
        else if (strcmp(key, "llm_base_url") == 0) strncpy(s_config.llm_base_url, val, 255);
        else if (strcmp(key, "llm_model") == 0) strncpy(s_config.llm_model, val, 63);
        else if (strcmp(key, "gpio_sda") == 0) s_config.gpio_sda = atoi(val);
        else if (strcmp(key, "gpio_scl") == 0) s_config.gpio_scl = atoi(val);
        else if (strcmp(key, "gpio_mic_sck") == 0) s_config.gpio_mic_sck = atoi(val);
        else if (strcmp(key, "gpio_mic_ws") == 0) s_config.gpio_mic_ws = atoi(val);
        else if (strcmp(key, "gpio_mic_sd") == 0) s_config.gpio_mic_sd = atoi(val);
        else if (strcmp(key, "gpio_spk_bclk") == 0) s_config.gpio_spk_bclk = atoi(val);
        else if (strcmp(key, "gpio_spk_lrc") == 0) s_config.gpio_spk_lrc = atoi(val);
        else if (strcmp(key, "gpio_spk_dout") == 0) s_config.gpio_spk_dout = atoi(val);
        else if (strcmp(key, "tenant_id") == 0) strncpy(s_config.tenant_id, val, 39);

        p = next;
    }

    /* Selective save based on form type to prevent wiping unrelated config */
    char query[64] = {0};
    httpd_req_get_url_query_str(req, query, sizeof(query));
    
    ESP_LOGI(TAG, "Configuration updated. Type: %s", query);
    
    if (strstr(query, "type=wifi")) {
        nvs_mgr_set_str(NVS_KEY_WIFI_SSID, s_config.wifi_ssid);
        nvs_mgr_set_str(NVS_KEY_WIFI_PASS, s_config.wifi_password);
        ESP_LOGI(TAG, "  WiFi saved");
    } else if (strstr(query, "type=tg")) {
        nvs_mgr_set_str(NVS_KEY_TG_TOKEN, s_config.tg_token);
        nvs_mgr_set_str(NVS_KEY_TG_CHAT_IDS, s_config.tg_userid);
        ESP_LOGI(TAG, "  Telegram saved");
    } else if (strstr(query, "type=llm")) {
        nvs_mgr_set_str(NVS_KEY_LLM_BACKEND, s_config.llm_type);
        nvs_mgr_set_str(NVS_KEY_LLM_API_KEY, s_config.llm_api_key);
        nvs_mgr_set_str(NVS_KEY_LLM_API_URL, s_config.llm_base_url);
        nvs_mgr_set_str(NVS_KEY_LLM_MODEL, s_config.llm_model);
        ESP_LOGI(TAG, "  LLM saved");
    } else if (strstr(query, "type=sys")) {
        nvs_mgr_set_str(NVS_KEY_TENANT_ID, s_config.tenant_id);
        ESP_LOGI(TAG, "  Tenant ID saved: %s", s_config.tenant_id);
    } else if (strstr(query, "type=gpio")) {
        nvs_mgr_set_i32("gpio_sda", s_config.gpio_sda);
        nvs_mgr_set_i32("gpio_scl", s_config.gpio_scl);
        nvs_mgr_set_i32("gpio_mic_sck", s_config.gpio_mic_sck);
        nvs_mgr_set_i32("gpio_mic_ws", s_config.gpio_mic_ws);
        nvs_mgr_set_i32("gpio_mic_sd", s_config.gpio_mic_sd);
        nvs_mgr_set_i32("gpio_spk_bclk", s_config.gpio_spk_bclk);
        nvs_mgr_set_i32("gpio_spk_lrc", s_config.gpio_spk_lrc);
        nvs_mgr_set_i32("gpio_spk_dout", s_config.gpio_spk_dout);
        ESP_LOGI(TAG, "  GPIO pins saved");
    } else {
        /* Fallback: save everything if type is unknown */
        wifi_ap_save_config(&s_config);
    }


    const char *resp_reboot = "<html><head><meta http-equiv='refresh' content='5;url=/'></head><body>WiFi Saved. Rebooting to connect...</body></html>";
    
    /* Always reboot after saving any config to ensure new values are loaded into services */
    httpd_resp_send(req, resp_reboot, strlen(resp_reboot));
    ESP_LOGI(TAG, "Config saved. Rebooting in 2s to apply...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

static const httpd_uri_t routes[] = {
    { .uri = "/", .method = HTTP_GET, .handler = root_handler },
    { .uri = "/save", .method = HTTP_POST, .handler = save_handler },
    { .uri = "/api/config/telegram", .method = HTTP_GET, .handler = api_telegram_get_handler },
    { .uri = "/api/config/telegram", .method = HTTP_POST, .handler = api_telegram_post_handler },
    { .uri = "/api/config/llm", .method = HTTP_GET, .handler = api_llm_get_handler },
    { .uri = "/api/config/llm", .method = HTTP_POST, .handler = api_llm_post_handler },
    { .uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler },
    { .uri = "/api/restart", .method = HTTP_POST, .handler = api_restart_handler },
};

/* ============================================================
 * JSON API Handlers
 * ============================================================ */

static esp_err_t api_telegram_get_handler(httpd_req_t *req)
{
    char token[128] = {0};
    char chat_ids[256] = {0};
    char buf[512];

    nvs_mgr_get_str(NVS_KEY_TG_TOKEN, token, sizeof(token));
    nvs_mgr_get_str(NVS_KEY_TG_CHAT_IDS, chat_ids, sizeof(chat_ids));

    int len = snprintf(buf, sizeof(buf),
        "{\"success\":true,\"bot_token\":\"%s\",\"chat_id\":\"%s\"}",
        token, chat_ids);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, len);
    return ESP_OK;
}

static esp_err_t api_telegram_post_handler(httpd_req_t *req)
{
    char buf[512] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    bool any_change = false;
    cJSON *token = cJSON_GetObjectItem(root, "bot_token");
    cJSON *chat = cJSON_GetObjectItem(root, "chat_id");

    if (token && cJSON_IsString(token) && strlen(token->valuestring) > 0) {
        esp_err_t err = nvs_mgr_set_str(NVS_KEY_TG_TOKEN, token->valuestring);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Telegram token saved via API");
            any_change = true;
        }
    }

    if (chat && cJSON_IsString(chat) && strlen(chat->valuestring) > 0) {
        esp_err_t err = nvs_mgr_set_str(NVS_KEY_TG_CHAT_IDS, chat->valuestring);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Telegram chat_id saved via API");
            any_change = true;
        }
    }

    cJSON_Delete(root);

    const char *resp = "{\"success\":true}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));

    if (any_change) {
        ESP_LOGI(TAG, "Telegram config updated via API — rebooting in 3s to apply...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }
    return ESP_OK;
}

static esp_err_t api_llm_get_handler(httpd_req_t *req)
{
    char buf[768];
    char backend[32] = "", api_key[256] = "", api_url[256] = "", model[128] = "";

    nvs_mgr_get_str(NVS_KEY_LLM_BACKEND, backend, sizeof(backend));
    nvs_mgr_get_str(NVS_KEY_LLM_API_KEY, api_key, sizeof(api_key));
    nvs_mgr_get_str(NVS_KEY_LLM_API_URL, api_url, sizeof(api_url));
    nvs_mgr_get_str(NVS_KEY_LLM_MODEL, model, sizeof(model));

    int len = snprintf(buf, sizeof(buf),
        "{\"provider\":\"%s\",\"api_key\":\"%s\",\"base_url\":\"%s\",\"model\":\"%s\"}",
        backend, api_key, api_url, model);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, len);
    return ESP_OK;
}

static esp_err_t api_llm_post_handler(httpd_req_t *req)
{
    char buf[768] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *prov = cJSON_GetObjectItem(root, "provider");
    cJSON *key  = cJSON_GetObjectItem(root, "api_key");
    cJSON *url  = cJSON_GetObjectItem(root, "base_url");
    cJSON *mod  = cJSON_GetObjectItem(root, "model");

    if (prov && cJSON_IsString(prov)) nvs_mgr_set_str(NVS_KEY_LLM_BACKEND, prov->valuestring);
    if (key  && cJSON_IsString(key))  nvs_mgr_set_str(NVS_KEY_LLM_API_KEY, key->valuestring);
    if (url  && cJSON_IsString(url))  nvs_mgr_set_str(NVS_KEY_LLM_API_URL, url->valuestring);
    if (mod  && cJSON_IsString(mod))  nvs_mgr_set_str(NVS_KEY_LLM_MODEL, mod->valuestring);

    ESP_LOGI(TAG, "LLM config updated via API — reboot needed");
    cJSON_Delete(root);

    const char *resp = "{\"success\":true,\"reboot\":true}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));

    ESP_LOGI(TAG, "Rebooting in 3s...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t api_status_handler(httpd_req_t *req)
{
    char buf[512];
    char ip_str[16] = "";
    extern char s_ip_str[16];

    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_heap  = esp_get_minimum_free_heap_size();

    int len = snprintf(buf, sizeof(buf),
        "{\"device_id\":\"%s\",\"free_heap\":%lu,\"min_heap\":%lu,"
        "\"wifi_connected\":true,\"mqtt_connected\":true}",
        "ESP32-S3", (unsigned long)free_heap, (unsigned long)min_heap);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, len);
    return ESP_OK;
}

static esp_err_t api_restart_handler(httpd_req_t *req)
{
    const char *resp = "{\"success\":true}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    ESP_LOGI(TAG, "Restart requested via API...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

/* ============================================================
 * DNS Server
 * ============================================================ */

static void dns_task(void *arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { vTaskDelete(NULL); return; }
    struct sockaddr_in serv_addr = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_ANY), .sin_port = htons(53) };
    bind(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    uint8_t buf[512];
    while (s_run_dns) {
        struct sockaddr_in client_addr; socklen_t addr_len = sizeof(client_addr);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&client_addr, &addr_len);
        if (len > 12 && buf[2] == 0x01) {
            buf[2] |= 0x80; buf[7] = 1;
            int resp_len = len;
            buf[resp_len++] = 0xC0; buf[resp_len++] = 0x0C;
            buf[resp_len++] = 0x00; buf[resp_len++] = 0x01;
            buf[resp_len++] = 0x00; buf[resp_len++] = 0x01;
            buf[resp_len++] = 0x00; buf[resp_len++] = 0x00; buf[resp_len++] = 0x00; buf[resp_len++] = 0x3C;
            buf[resp_len++] = 0x00; buf[resp_len++] = 0x04;
            buf[resp_len++] = 192;  buf[resp_len++] = 168; buf[resp_len++] = 4; buf[resp_len++] = 1;
            sendto(sock, buf, resp_len, 0, (struct sockaddr*)&client_addr, addr_len);
        }
    }
    closesocket(sock); s_dns_task = NULL; vTaskDelete(NULL);
}

/* ============================================================
 * Core Implementation
 * ============================================================ */

esp_err_t wifi_ap_start(void) {
    if (s_ap_active) return ESP_OK;

    /* Load current config from NVS */
    wifi_ap_load_config_internal();

    wifi_mgr_init_common();
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap_netif) ap_netif = esp_netif_create_default_wifi_ap();

    uint8_t mac[6]; esp_wifi_get_mac(WIFI_IF_AP, mac);
    char ssid[32]; snprintf(ssid, sizeof(ssid), "ESPClaw-%02X%02X", mac[4], mac[5]);
    wifi_config_t wifi_config = { .ap = { .channel = AP_CHANNEL, .max_connection = 4, .authmode = WIFI_AUTH_WPA_WPA2_PSK } };
    strncpy((char*)wifi_config.ap.ssid, ssid, 31); strncpy((char*)wifi_config.ap.password, AP_PASSWORD, 63);

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    mdns_init(); mdns_hostname_set("espclaw"); mdns_instance_name_set("ESPClaw Config");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    if (!s_httpd_active) http_server_start();
    s_run_dns = true; xTaskCreate(dns_task, "dns", 4096, NULL, 3, &s_dns_task);
    s_ap_active = true;
    return ESP_OK;
}

esp_err_t http_server_start(void) {
    if (s_httpd_active) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 16384;
    config.server_port = 80;
    if (httpd_start(&s_httpd, &config) == ESP_OK) {
        for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
            httpd_register_uri_handler(s_httpd, &routes[i]);
        }
        s_httpd_active = true;
        ESP_LOGI(TAG, "HTTP server started on port 80");
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t http_server_stop(void)
{
    if (s_httpd) {
        ESP_LOGI(TAG, "Stopping HTTP server to free memory...");
        httpd_stop(s_httpd);
        s_httpd = NULL;
        s_httpd_active = false;
    }
    return ESP_OK;
}

esp_err_t wifi_ap_stop(void) {
    if (!s_ap_active) return ESP_OK;
    s_run_dns = false;
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = NULL; s_httpd_active = false; }
    mdns_free(); esp_wifi_stop(); s_ap_active = false; return ESP_OK;
}

bool wifi_ap_is_active(void) { return s_ap_active; }

esp_err_t wifi_ap_save_config(const espclaw_wifi_ap_config_t *config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    
    ESP_LOGI(TAG, "Saving configuration to NVS...");
    
    nvs_mgr_set_str(NVS_KEY_WIFI_SSID, config->wifi_ssid);
    ESP_LOGI(TAG, "  WiFi SSID saved: %s", config->wifi_ssid);
    nvs_mgr_set_str(NVS_KEY_WIFI_PASS, config->wifi_password);
    ESP_LOGI(TAG, "  WiFi Pass saved");
    nvs_mgr_set_str(NVS_KEY_TG_TOKEN, config->tg_token);
    ESP_LOGI(TAG, "  Telegram Token saved");
    nvs_mgr_set_str(NVS_KEY_TG_CHAT_IDS, config->tg_userid);
    ESP_LOGI(TAG, "  Telegram User ID saved: %s", config->tg_userid);
    nvs_mgr_set_str(NVS_KEY_TENANT_ID, config->tenant_id);
    ESP_LOGI(TAG, "  Tenant ID saved: %s", config->tenant_id);
    nvs_mgr_set_str(NVS_KEY_LLM_BACKEND, config->llm_type);
    nvs_mgr_set_str(NVS_KEY_LLM_API_KEY, config->llm_api_key);
    nvs_mgr_set_str(NVS_KEY_LLM_API_URL, config->llm_base_url);
    nvs_mgr_set_str(NVS_KEY_LLM_MODEL, config->llm_model);
    nvs_mgr_set_i32("gpio_sda", config->gpio_sda);
    nvs_mgr_set_i32("gpio_scl", config->gpio_scl);
    nvs_mgr_set_i32("gpio_mic_sck", config->gpio_mic_sck);
    nvs_mgr_set_i32("gpio_mic_ws", config->gpio_mic_ws);
    nvs_mgr_set_i32("gpio_mic_sd", config->gpio_mic_sd);
    nvs_mgr_set_i32("gpio_spk_bclk", config->gpio_spk_bclk);
    nvs_mgr_set_i32("gpio_spk_lrc", config->gpio_spk_lrc);
    nvs_mgr_set_i32("gpio_spk_dout", config->gpio_spk_dout);
    return ESP_OK;
}
