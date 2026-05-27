/*
 * ESPClaw - channel/channel_mqtt.c
 * MQTT channel for IoT control — bidirectional communication via MQTT broker.
 *
 * Features:
 *   - Subscribe to command topic (espclaw/{client_id}/cmd)
 *   - Publish responses to topic (espclaw/{client_id}/response)
 *   - Optional TLS/SSL support
 *   - Last Will and Testament (LWT) for connection status
 *   - QoS 1 for reliable delivery
 *
 * Usage:
 *   1. Configure MQTT broker URL and credentials via menuconfig
 *   2. Publish commands to: espclaw/{device_id}/cmd
 *   3. Receive responses on: espclaw/{device_id}/response
 *   4. Monitor status on: espclaw/{device_id}/status (online/offline)
 */
#include "channel/channel.h"
#include "bus/message_bus.h"
#include "messages.h"
#include "config.h"
#include "platform.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include "display_ui.h"
#include "esp_crt_bundle.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "nvs_keys.h"
#include "mem/nvs_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "net/neuron_link.h"
#if ESPCLAW_HAS_WAKEWORD
#include "wakeword/wakeword.h"
#endif
#include <stdio.h>
#include <string.h>

extern const char *get_device_id(void);

static const char *TAG = "mqtt_ch";

/* Runtime state */
static message_bus_t *s_bus = NULL;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static char s_client_id[32] = {0};
static char s_topic_cmd[64] = {0};
static char s_topic_response[64] = {0};
static char s_topic_status[64] = {0};
static char s_topic_otp[64] = {0};
static char s_topic_login_success[64] = {0};
static char s_backend_url[256] = {0};
static bool s_connected = false;
static bool s_heartbeat_started = false;

static void mqtt_trigger_sync_task(void *arg)
{
    ESP_LOGI(TAG, "MQTT sync task: pushing local changes to cloud (delta sync)...");
    /* Delta sync = device-to-cloud only, preserves local config */
    esp_err_t err = neuron_link_sync(SYNC_MODE_DELTA, SYNC_DIR_DEVICE_TO_CLOUD, NULL);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT sync task: push completed successfully");
    } else {
        ESP_LOGW(TAG, "MQTT sync task: push failed: %s", esp_err_to_name(err));
    }
    vTaskDelete(NULL);
    (void)arg;
}

static void mqtt_action_sync_task(void *arg)
{
    ESP_LOGI(TAG, "MQTT sync task (via cmd): pushing local changes...");
    neuron_link_sync(SYNC_MODE_DELTA, SYNC_DIR_DEVICE_TO_CLOUD, NULL);
    vTaskDelete(NULL);
}

static void mqtt_action_pull_config_task(void *arg)
{
    ESP_LOGI(TAG, "MQTT pull config task (via cmd): cloud->device config sync...");
    neuron_link_sync_config_only();
    vTaskDelete(NULL);
}

/* Heartbeat HTTP endpoint (optional) */
static void mqtt_send_heartbeat(void *arg)
{
    if (!s_connected || strlen(s_backend_url) == 0) return;

    char url[384];
    snprintf(url, sizeof(url), "%s/api/device/heartbeat", s_backend_url);

    char body[128];
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", s_client_id);

    typedef struct { char resp[256]; } http_ctx_t;
    http_ctx_t ctx = { .resp = {0} };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 256,
        .user_data = &ctx,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200) {
        ESP_LOGI(TAG, "Heartbeat OK");
    } else {
        ESP_LOGW(TAG, "Heartbeat failed: status=%d err=%s", status, esp_err_to_name(err));
    }
}

static void mqtt_load_backend_url(void)
{
    /* NVS override */
    if (nvs_mgr_get_str(NVS_KEY_BACKEND_URL, s_backend_url, sizeof(s_backend_url))
        && strlen(s_backend_url) > 0) {
        ESP_LOGI(TAG, "Backend URL from NVS: %s", s_backend_url);
        return;
    }
#ifdef CONFIG_ESPCLAW_BACKEND_URL
    strncpy(s_backend_url, CONFIG_ESPCLAW_BACKEND_URL, sizeof(s_backend_url) - 1);
    ESP_LOGI(TAG, "Backend URL from Kconfig: %s", s_backend_url);
#else
    s_backend_url[0] = '\0';
    ESP_LOGI(TAG, "No backend URL configured");
#endif
}

/* Outbound queue: stores POINTERS to PSRAM-allocated messages.
 * This avoids copying large payloads into Internal RAM queue slots.
 * Each item in queue is a mqtt_out_msg_t* (4 bytes) instead of ~2KB. */
static QueueHandle_t s_mqtt_outbox = NULL;
#define MQTT_OUTBOX_SIZE        8
#define MQTT_MAX_PAYLOAD_LEN    2048

typedef struct {
    char topic[64];
    char payload[MQTT_MAX_PAYLOAD_LEN];
    int qos;
    bool retain;
} mqtt_out_msg_t;

static void mqtt_generate_client_id(void)
{
    const char *device_id = get_device_id();
    if (!device_id || strlen(device_id) == 0) {
        device_id = "espclaw_unknown";
    }

    strncpy(s_client_id, device_id, sizeof(s_client_id) - 1);

    snprintf(s_topic_cmd, sizeof(s_topic_cmd),
             "espclaw/%s/cmd", s_client_id);
    snprintf(s_topic_response, sizeof(s_topic_response),
             "espclaw/%s/response", s_client_id);
    snprintf(s_topic_status, sizeof(s_topic_status),
             "espclaw/%s/status", s_client_id);
    snprintf(s_topic_otp, sizeof(s_topic_otp),
             "espclaw/%s/otp", s_client_id);
  snprintf(s_topic_login_success, sizeof(s_topic_login_success),
             "espclaw/%s/login_success", s_client_id);

    ESP_LOGE(TAG, "Client ID: %s", s_client_id);
    ESP_LOGE(TAG, "Subscribe Cmd: %s", s_topic_cmd);
    ESP_LOGE(TAG, "Subscribe OTP: %s", s_topic_otp);
    ESP_LOGE(TAG, "Subscribe Login Success: %s", s_topic_login_success);
    ESP_LOGE(TAG, "Publish Status: %s", s_topic_status);
}

/* -----------------------------------------------------------------------
 * MQTT event handler
 * ----------------------------------------------------------------------- */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGE(TAG, "!!! MQTT CONNECTED TO BROKER !!!");
            s_connected = true;

            /* Subscribe to command topic */
            esp_mqtt_client_subscribe(event->client, s_topic_cmd, 1);
            /* Subscribe to OTP topic */
            esp_mqtt_client_subscribe(event->client, s_topic_otp, 1);
            /* Subscribe to login success topic */
            esp_mqtt_client_subscribe(event->client, s_topic_login_success, 1);
            ESP_LOGI(TAG, "Subscribed to cmd and otp topics");

            /* Publish online status */
            esp_mqtt_client_publish(event->client, s_topic_status,
                                    "online", 6, 1, 1);

            /* Send HTTP heartbeat to backend */
            mqtt_send_heartbeat(NULL);

            /* Trigger Neuron Link delta sync when MQTT connects.
             * Only PUSHES local changes to cloud (device-to-cloud).
             * Cloud-to-device sync only happens on explicit user request
             * via MQTT command or agent tool — to preserve local config. */
            if (neuron_link_is_paired()) {
                ESP_LOGI(TAG, "MQTT connected, device is paired — pushing local changes to cloud...");
                BaseType_t created = xTaskCreate(mqtt_trigger_sync_task, "mqtt_sync",
                            10240, NULL, CHANNEL_TASK_PRIORITY - 1, NULL);
                if (created != pdPASS) {
                    ESP_LOGE(TAG, "Failed to create mqtt_sync task!");
                }
            } else {
                ESP_LOGI(TAG, "MQTT connected but device NOT paired — skipping sync");
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            s_connected = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGD(TAG, "Subscribed ack, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGD(TAG, "Unsubscribed ack, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "Published ack, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            /* Handle OTP message */
            if (event->topic_len == strlen(s_topic_otp) && 
                strncmp(event->topic, s_topic_otp, event->topic_len) == 0) {
                char *payload = malloc(event->data_len + 1);
                if (payload) {
                    memcpy(payload, event->data, event->data_len);
                    payload[event->data_len] = '\0';

                    char *otp_pos = strstr(payload, "\"otp\":\"");
                    if (otp_pos && strlen(otp_pos) > 7) {
                        char otp_code[7] = {0};
                        memcpy(otp_code, otp_pos + 7, 6);
                        ESP_LOGI(TAG, "OTP received: %s", otp_code);
                        display_ui_show_message("LOGIN REQUEST", "Code:", otp_code);
                    } else if (strstr(payload, "\"status\":\"verified\"")) {
                        ESP_LOGI(TAG, "Login verified via OTP topic");
                        display_ui_set_state(DISPLAY_STATE_READY);
#if CONFIG_ESPCLAW_CHANNEL_TELEGRAM
                        telegram_post("Device login verified via OTP.", 0);
#endif
                    } else {
                        ESP_LOGW(TAG, "Unknown payload on OTP topic: %s", payload);
                    }
                    free(payload);
                }
                break;
            }

            /* Handle Login Success message */
            if (event->topic_len == strlen(s_topic_login_success) && 
                strncmp(event->topic, s_topic_login_success, event->topic_len) == 0) {
                char *json_data = malloc(event->data_len + 1);
                if (json_data) {
                    memcpy(json_data, event->data, event->data_len);
                    json_data[event->data_len] = '\0';
                    
                    cJSON *root = cJSON_Parse(json_data);
                    if (root) {
                        cJSON *status = cJSON_GetObjectItem(root, "status");
                        if (status && cJSON_IsString(status) && strcmp(status->valuestring, "success") == 0) {
                            cJSON *tenant_id = cJSON_GetObjectItem(root, "tenant_id");
                            if (tenant_id && cJSON_IsString(tenant_id)) {
                                ESP_LOGI(TAG, "Login success! Tenant ID: %s", tenant_id->valuestring);
                                nvs_mgr_set_str("tenant_id", tenant_id->valuestring);
                                
                                /* Update Neuron Link in memory so next sync uses the new tenant */
                                extern esp_err_t neuron_link_set_tenant_id(const char *tenant_id);
                                neuron_link_set_tenant_id(tenant_id->valuestring);

                                display_ui_set_state(DISPLAY_STATE_READY);
                                
#if CONFIG_ESPCLAW_CHANNEL_TELEGRAM
                                telegram_post("Device logged in to backend successfully!", 0);
#endif
                                
                                /* Trigger Neuron Link delta sync to push local config to cloud.
                                 * Only pushes local changes (device→cloud).
                                 * This preserves any local config that was set on the device
                                 * before pairing, rather than overwriting it from cloud. */
                                ESP_LOGI(TAG, "Triggering delta sync (push local to cloud)...");
                                neuron_link_sync(SYNC_MODE_DELTA, SYNC_DIR_DEVICE_TO_CLOUD, NULL);
                            }
                        }
                        cJSON_Delete(root);
                    }
                    free(json_data);
                }
                break;
            }

            /* Handle System Commands */
            if (strncmp(event->topic, s_topic_cmd, event->topic_len) == 0) {
                char *payload = malloc(event->data_len + 1);
                if (payload) {
                    memcpy(payload, event->data, event->data_len);
                    payload[event->data_len] = '\0';

                    if (strstr(payload, "\"action\":\"sync\"")) {
                        ESP_LOGI(TAG, "Sync command received via MQTT — spawning task");
                        xTaskCreate(mqtt_action_sync_task, "mqtt_cmd_sync", 10240, NULL, CHANNEL_TASK_PRIORITY - 1, NULL);
                        free(payload);
                        break;
                    }
                    if (strstr(payload, "\"action\":\"pull_config\"")) {
                        ESP_LOGI(TAG, "Pull config command received — spawning task");
                        xTaskCreate(mqtt_action_pull_config_task, "mqtt_cmd_pull", 10240, NULL, CHANNEL_TASK_PRIORITY - 1, NULL);
                        free(payload);
                        break;
                    }

                    /* Mic test: capture PCM from I2S and stream to espclaw/{id}/audio */
                    if (strstr(payload, "\"type\":\"mic_test\"")) {
                        extern void mic_test_handle_command(const char *payload, size_t len);
                        mic_test_handle_command(payload, event->data_len);
                        free(payload);
                        break;
                    }

                    /* Diagnostic: echo test */
                    if (strstr(payload, "\"type\":\"diag\"")) {
                        ESP_LOGI(TAG, "DIAG cmd received");
                        extern void mqtt_handle_diag_echo(const char *p, size_t len, const char *topic);
                        /* Extract echo_topic from payload */
                        const char *et = strstr(payload, "\"echo_topic\":\"");
                        if (!et) et = strstr(payload, "\"echo_topic\": \"");
                        if (et) {
                            const char *start = strchr(et + 13, '"');
                            if (start) {
                                char echo_topic[64] = {0};
                                size_t copy_len = start - (et + 13);
                                if (copy_len >= sizeof(echo_topic)) copy_len = sizeof(echo_topic) - 1;
                                strncpy(echo_topic, et + 13, copy_len);
                                mqtt_handle_diag_echo(payload, event->data_len, echo_topic);
                            }
                        }
                        free(payload);
                        break;
                    }

#if ESPCLAW_HAS_WAKEWORD
                    if (wakeword_handle_mqtt_json(payload)) {
                        ESP_LOGI(TAG, "Wake word MQTT command handled");
                        free(payload);
                        break;
                    }
#endif
                    free(payload);
                }
            }

            /* 2. Forward regular Cmd to agent via inbound queue */
            if (s_bus && event->data_len > 0 && event->data_len < MAX_MESSAGE_LEN) {
                inbound_msg_t msg = {0};
                memcpy(msg.text, event->data,
                       event->data_len < sizeof(msg.text) - 1 ?
                       event->data_len : sizeof(msg.text) - 1);
                msg.source = MSG_SOURCE_MQTT;
                msg.chat_id = 0;  /* MQTT doesn't use chat_id */

                /* Wait for agent to be ready (up to 60s to match LLM timeout) */
                if (message_bus_post_inbound(s_bus, &msg, pdMS_TO_TICKS(60000)) != pdPASS) {
                    ESP_LOGW(TAG, "Failed to post inbound message (queue full, timeout)");
                }
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            if (event->error_handle) {
                ESP_LOGE(TAG, "  type=%d code=%d",
                         event->error_handle->error_type,
                         event->error_handle->connect_return_code);
            }
            break;

        case MQTT_EVENT_BEFORE_CONNECT:
            break;

        default:
            ESP_LOGD(TAG, "MQTT event id=%d", event->event_id);
            break;
    }
}

/* -----------------------------------------------------------------------
 * MQTT publish task — handles outbound messages from agent
 * ----------------------------------------------------------------------- */
static void mqtt_publish_task(void *arg)
{
    mqtt_out_msg_t *msg_ptr;

    while (1) {
        if (xQueueReceive(s_mqtt_outbox, &msg_ptr, portMAX_DELAY) == pdTRUE) {
            if (msg_ptr) {
                if (s_connected && s_mqtt_client) {
                    int msg_id = esp_mqtt_client_publish(
                        s_mqtt_client,
                        msg_ptr->topic,
                        msg_ptr->payload,
                        0,
                        msg_ptr->qos,
                        msg_ptr->retain
                    );
                    ESP_LOGD(TAG, "Published to %s (msg_id=%d)", msg_ptr->topic, msg_id);
                } else {
                    ESP_LOGW(TAG, "MQTT not connected, dropping message");
                }
                free(msg_ptr); /* free PSRAM allocation */
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * Channel ops vtable
 * ----------------------------------------------------------------------- */
static esp_err_t mqtt_start(message_bus_t *bus)
{
    if (s_mqtt_client) {
        return ESP_OK;
    }
    s_bus = bus;

    /* Initialize SNTP for time sync (required for TLS) */
    if (!esp_sntp_enabled()) {
        ESP_LOGI(TAG, "Initializing SNTP...");
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
    }

    /* Generate client ID and topics */
    mqtt_generate_client_id();
    mqtt_load_backend_url();

    ESP_LOGI(TAG, "=== MQTT INIT ===");

    /* Create outbound queue — stores pointers, not copies */
    s_mqtt_outbox = xQueueCreate(MQTT_OUTBOX_SIZE, sizeof(mqtt_out_msg_t *));
    if (!s_mqtt_outbox) {
        ESP_LOGE(TAG, "Failed to create MQTT outbox queue");
        return ESP_ERR_NO_MEM;
    }

    /* Silence verbose TLS/MQTT logs */
    esp_log_level_set("mqtt_client", ESP_LOG_WARN);
    esp_log_level_set("esp-tls", ESP_LOG_ERROR);
    esp_log_level_set("transport", ESP_LOG_WARN);
    esp_log_level_set("mbedtls", ESP_LOG_ERROR);
    esp_log_level_set("mqtt_ch", ESP_LOG_INFO);

    /* Create MQTT client config */
    static const char *alpn_protos[] = { "mqtt", NULL };

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = "e855d1adcb91498097194e25175017dd.s1.eu.hivemq.cloud",
        .broker.address.port = 8883,
        .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
        .credentials.client_id = s_client_id,
        .credentials.username = "myesp1456",
        .credentials.authentication.password = "Myesp1456",
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .broker.verification.alpn_protos = alpn_protos,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        .session.keepalive = 60,
        .network.timeout_ms = 20000,
        .task.stack_size = MQTT_TASK_STACK_SIZE, /* Correct field for ESP-IDF v5.x */
    };

    /* Create MQTT client */
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt_client) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return ESP_FAIL;
    }

    /* Register event handler */
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);

    /* Start MQTT client */
    esp_err_t err = esp_mqtt_client_start(s_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT: %s", esp_err_to_name(err));
        return err;
    }

    /* Start publish task */
    xTaskCreate(mqtt_publish_task, "mqtt_pub",
                MQTT_TASK_STACK_SIZE, NULL,
                CHANNEL_TASK_PRIORITY, NULL);

    /* Start periodic heartbeat timer (every 30s) */
    if (strlen(s_backend_url) > 0 && !s_heartbeat_started) {
        static esp_timer_handle_t s_hb_timer;
        const esp_timer_create_args_t args = {
            .callback = &mqtt_send_heartbeat,
            .arg = NULL,
            .name = "heartbeat",
        };
        esp_timer_create(&args, &s_hb_timer);
        esp_timer_start_periodic(s_hb_timer, 30 * 1000000);
        s_heartbeat_started = true;
        ESP_LOGI(TAG, "Heartbeat timer started");
    }

    ESP_LOGI(TAG, "MQTT channel started");
    return ESP_OK;
}

static bool mqtt_is_available(void)
{
    /* MQTT is available if broker URL is configured */
    return strlen(CONFIG_ESPCLAW_MQTT_BROKER_URL) > 0;
}

const channel_ops_t mqtt_channel_ops = {
    .name = "mqtt",
    .start = mqtt_start,
    .is_available = mqtt_is_available,
};

/* -----------------------------------------------------------------------
 * Public API — post message to MQTT
 * ----------------------------------------------------------------------- */
#ifdef CONFIG_ESPCLAW_CHANNEL_MQTT
void mqtt_post(const char *text)
{
    if (!s_mqtt_outbox || !text) {
        return;
    }

    /* Alloc in PSRAM to avoid Internal RAM pressure */
    mqtt_out_msg_t *msg = heap_caps_malloc(sizeof(mqtt_out_msg_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!msg) {
        ESP_LOGE(TAG, "OOM for mqtt_post (PSRAM)");
        return;
    }
    memset(msg, 0, sizeof(*msg));
    strncpy(msg->topic, s_topic_response, sizeof(msg->topic) - 1);
    strncpy(msg->payload, text, sizeof(msg->payload) - 1);
    msg->qos = 1;
    msg->retain = 0;

    if (xQueueSend(s_mqtt_outbox, &msg, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGW(TAG, "MQTT outbox full, message dropped");
        free(msg);
    }
}

/* Check if MQTT is connected */
bool mqtt_is_connected(void)
{
    return s_connected;
}

/* Get client ID */
const char *mqtt_get_client_id(void)
{
    return s_client_id;
}

/* Get command topic */
const char *mqtt_get_cmd_topic(void)
{
    return s_topic_cmd;
}

/* Get response topic */
const char *mqtt_get_response_topic(void)
{
    return s_topic_response;
}

/* -----------------------------------------------------------------------
 * Public API — publish raw binary to any topic (used by mic_test.c)
 * ----------------------------------------------------------------------- */
void mqtt_publish_binary(const char *topic, const void *data, size_t len)
{
    if (!s_mqtt_outbox || !topic || !data || len == 0) {
        return;
    }

    mqtt_out_msg_t *msg = heap_caps_malloc(sizeof(mqtt_out_msg_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!msg) {
        ESP_LOGE(TAG, "OOM for mqtt_publish_binary (PSRAM)");
        return;
    }
    memset(msg, 0, sizeof(*msg));
    strncpy(msg->topic, topic, sizeof(msg->topic) - 1);

    size_t copy_len = len < sizeof(msg->payload) ? len : sizeof(msg->payload) - 1;
    memcpy(msg->payload, data, copy_len);
    msg->payload[copy_len] = '\0';
    msg->qos = 1;
    msg->retain = 0;

    if (xQueueSend(s_mqtt_outbox, &msg, pdMS_TO_TICKS(200)) != pdPASS) {
        ESP_LOGW(TAG, "Outbox full, binary publish dropped (%d bytes)", (int)len);
        free(msg);
    } else {
        ESP_LOGD(TAG, "Binary publish queued: topic=%s len=%d", topic, (int)len);
    }
}

/* Public API — publish text to any topic */
void mqtt_publish_text(const char *topic, const char *text)
{
    if (!s_mqtt_outbox || !topic || !text) return;
    mqtt_publish_binary(topic, text, strlen(text));
}

/* Echo diagnostic — respond on a given topic (used by mic-test debug) */
void mqtt_handle_diag_echo(const char *payload, size_t len,
                           const char *echo_topic)
{
    ESP_LOGI(TAG, "DIAG echo: from=%s len=%d", echo_topic, (int)len);
    char resp[256];
    int written = snprintf(resp, sizeof(resp),
                          "{\"type\":\"diag_echo\",\"from\":\"%s\",\"ts\":%llu}",
                          echo_topic, (unsigned long long)esp_timer_get_time());
    mqtt_publish_text(echo_topic, resp);
}
#endif /* CONFIG_ESPCLAW_CHANNEL_MQTT */
