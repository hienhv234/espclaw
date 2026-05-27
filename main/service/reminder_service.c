/*
 * ESPClaw - service/reminder_service.c
 *
 * Polls backend /api/reminders for due reminders every minute.
 * Queues notification to agent loop → TTS/display on ESP.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "reminder_service.h"
#include "service/cron_service.h"
#include "bus/message_bus.h"
#include "messages.h"
#include "nvs_keys.h"
#include "mem/nvs_manager.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define REMTAG(x) ((void)(x))

static const char *TAG = "reminder";

static QueueHandle_t g_rem_q = NULL;
static SemaphoreHandle_t g_rem_mx = NULL;
static int g_rem_on = 0;
static char g_rem_url[128] = {0};

typedef struct {
    char id[64];
    uint64_t ts;
} rslot_t;

static rslot_t g_rem_fired[8];
static int g_rem_fired_idx = 0;

static int lock_rem(void) {
    return g_rem_mx && xSemaphoreTake(g_rem_mx, pdMS_TO_TICKS(500)) == pdTRUE;
}
static void unlock_rem(void) {
    if (g_rem_mx) xSemaphoreGive(g_rem_mx);
}

static int was_fired(const char *id) {
    uint64_t now = esp_log_timestamp();
    for (int i = 0; i < 8; i++) {
        if (g_rem_fired[i].id[0] && strcmp(g_rem_fired[i].id, id) == 0) {
            if (now - g_rem_fired[i].ts < 300000) return 1;
            g_rem_fired[i].id[0] = '\0';
        }
    }
    return 0;
}

static void mark_fired(const char *id) {
    int s = g_rem_fired_idx % 8;
    strncpy(g_rem_fired[s].id, id, 63);
    g_rem_fired[s].id[63] = '\0';
    g_rem_fired[s].ts = esp_log_timestamp();
    g_rem_fired_idx++;
}

static void load_rem_cfg(void) {
    char buf[128] = {0};
    if (nvs_mgr_get_str(NVS_KEY_BACKEND_URL, buf, sizeof(buf)) && buf[0]) {
        strncpy(g_rem_url, buf, sizeof(g_rem_url) - 1);
        g_rem_on = 1;
        ESP_LOGI(TAG, "Backend URL: %s", g_rem_url);
    } else {
        g_rem_on = 0;
        ESP_LOGW(TAG, "No backend URL — reminder disabled");
    }
}

typedef struct {
    char id[64];
    char appt_id[64];
    char title[128];
    char trig_at[32];
    char channel[32];
    char desc[256];
} ritem_t;

static int fetch_reminders(ritem_t *out, int max) {
    if (!g_rem_on || !g_rem_url[0]) return 0;

    char tid[64] = {0};
    char did[64] = {0};
    nvs_mgr_get_str(NVS_KEY_TENANT_ID, tid, sizeof(tid));
    nvs_mgr_get_str(NVS_KEY_DEVICE_ID, did, sizeof(did));
    if (!tid[0]) return 0;

    char url[384];
    int ul = snprintf(url, sizeof(url),
        "%s/api/reminders?tenant_id=%s&window=%d",
        g_rem_url, tid, 5);
    if (did[0] && ul < (int)sizeof(url))
        snprintf(url + ul, sizeof(url) - ul, "&device_id=%s", did);

    char *body = malloc(4096);
    if (!body) return 0;
    memset(body, 0, 4096);
    size_t blen = 0;

    esp_http_client_config_t cfg = { .url = url, .timeout_ms = 10000 };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) {
        free(body);
        return 0;
    }

    esp_http_client_set_header(cl, "Content-Type", "application/json");
    esp_http_client_set_header(cl, "Accept", "application/json");
    esp_err_t err = esp_http_client_perform(cl);
    if (err && esp_http_client_get_status_code(cl) == 200)
        blen = esp_http_client_read(cl, body, 4096 - 1);
    esp_http_client_cleanup(cl);
    
    if (blen <= 0) {
        free(body);
        return 0;
    }

    /* Simple JSON parser */
    const char *p = body;
    int depth = 0;
    const char *obj = NULL;
    ritem_t *it = NULL;
    int cnt = 0;

    while (*p && cnt < max) {
        if (*p == '{') {
            if (depth == 0) { obj = p; it = &out[cnt]; memset(it, 0, sizeof(*it)); }
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0 && obj && it) {
                char fld[512] = {0};
                size_t fl = p - obj + 1;
                if (fl < sizeof(fld)) {
                    memcpy(fld, obj, fl);
                    #define X(FLD, DST) do { \
                        const char *fp = strstr(fld, "\"" #FLD "\""); \
                        if (fp) { \
                            const char *q = strchr(fp + sizeof(#FLD), '"'); \
                            if (q) { \
                                const char *qe = strchr(q + 1, '"'); \
                                if (qe) { \
                                    size_t vl = qe - q - 1; \
                                    if (vl > 0 && vl < sizeof((it)->DST) - 1) \
                                        memcpy((it)->DST, q + 1, vl); \
                                } \
                            } \
                        } \
                    } while (0)
                    X(id, id);
                    X(appointment_id, appt_id);
                    X(title, title);
                    X(trigger_at, trig_at);
                    X(notify_channel, channel);
                    X(description, desc);
                    #undef X
                    cnt++;
                }
                obj = NULL; it = NULL;
            }
        }
        p++;
    }
    free(body);
    return cnt;
}

static void send_ack(const char *rid) {
    if (!g_rem_on || !g_rem_url[0] || !rid[0]) return;
    char url[384];
    snprintf(url, sizeof(url), "%s/api/reminders/acknowledge", g_rem_url);
    char body[512];
    snprintf(body, sizeof(body), "{\"reminder_id\":\"%.128s\"}", rid);
    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 5000 };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl) return;
    esp_http_client_set_header(cl, "Content-Type", "application/json");
    esp_http_client_set_post_field(cl, body, strlen(body));
    esp_http_client_perform(cl);
    esp_http_client_cleanup(cl);
}

static void enque_notif(const ritem_t *it) {
    inbound_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.source = MSG_SOURCE_REMINDER;
    if (it->desc[0])
        snprintf(msg.text, sizeof(msg.text), "[reminder] %s. %s", it->title, it->desc);
    else
        snprintf(msg.text, sizeof(msg.text), "[reminder] Đã đến giờ: %s", it->title);
    if (xQueueSend(g_rem_q, &msg, pdMS_TO_TICKS(500)) != pdTRUE)
        ESP_LOGW(TAG, "Dropped: %s", it->title);
    else
        ESP_LOGI(TAG, "Queued: %s", it->title);
}

static void rem_task(void *arg) {
    (void)arg;
    ritem_t *items = malloc(sizeof(ritem_t) * 8);
    if (!items) {
        ESP_LOGE(TAG, "Failed to allocate items memory");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Task started");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        if (!cron_is_time_synced()) continue;
        if (!lock_rem()) continue;
        if (!g_rem_on) { unlock_rem(); continue; }
        int n = fetch_reminders(items, 8);
        unlock_rem();
        for (int i = 0; i < n; i++) {
            if (was_fired(items[i].id)) continue;
            mark_fired(items[i].id);
            enque_notif(&items[i]);
            send_ack(items[i].id);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

esp_err_t reminder_service_init(void) {
    if (!g_rem_mx) {
        g_rem_mx = xSemaphoreCreateMutex();
        if (!g_rem_mx) return ESP_ERR_NO_MEM;
    }
    load_rem_cfg();
    ESP_LOGI(TAG, "Init (on=%d)", g_rem_on);
    return ESP_OK;
}

esp_err_t reminder_service_start(QueueHandle_t q) {
    if (!q) return ESP_ERR_INVALID_ARG;
    g_rem_q = q;
    BaseType_t r = xTaskCreate(rem_task, "reminder", 4096, NULL, 4, NULL);
    if (r != pdPASS) return ESP_FAIL;
    ESP_LOGI(TAG, "Started");
    return ESP_OK;
}

void reminder_service_set_enabled(bool enabled) {
    if (lock_rem()) { g_rem_on = enabled; unlock_rem(); }
}

bool reminder_service_is_enabled(void) {
    bool v = false;
    if (lock_rem()) { v = g_rem_on; unlock_rem(); }
    return v;
}

void reminder_service_refresh_config(void) {
    if (lock_rem()) { load_rem_cfg(); unlock_rem(); }
}
