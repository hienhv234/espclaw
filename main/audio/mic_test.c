/*
 * ESPClaw - audio/mic_test.c
 * Microphone test: capture PCM from I2S mic and stream raw binary to MQTT.
 * Triggered by backend via MQTT command "type":"mic_test".
 *
 * Flow:
 *   1. Backend sends: {"type":"mic_test","action":"start","duration_ms":1800}
 *   2. ESP starts capture task → publishes PCM chunks to espclaw/{id}/audio
 *   3. ESP sends end marker: {"type":"mic_test_done","chunks":N,"samples":S}
 *   4. Backend assembles chunks → WAV → returns to frontend
 */

#include "mic_test.h"
#include "i2s_capture.h"
#include "wakeword/wakeword.h"
#include "display_ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mem/nvs_manager.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "mic_test";

/* Default capture: 1.8s @ 16 kHz mono 16-bit = 28800 samples = 57600 bytes */
#define MIC_TEST_DEFAULT_MS   1800
#define MIC_TEST_MAX_MS      10000
#define MIC_TEST_CHUNK_SAMPLES (I2S_CAPTURE_FRAME_SAMPLES)  /* 512 samples = 1024 bytes */
#define MIC_TEST_CHUNK_BYTES  (MIC_TEST_CHUNK_SAMPLES * sizeof(int16_t))

static TaskHandle_t s_mic_test_task;
static volatile bool s_capturing;

/* ------------------------------------------------------------------ */
/* Audio streaming via public MQTT API (channel_mqtt.c) */

extern void mqtt_publish_binary(const char *topic, const void *data, size_t len);
extern void mqtt_publish_text(const char *topic, const char *text);
extern const char *get_device_id(void);

static char s_audio_topic[64];

static void build_audio_topic(void)
{
    const char *dev = get_device_id();
    snprintf(s_audio_topic, sizeof(s_audio_topic),
             "espclaw/%s/audio", dev ? dev : "unknown");
}

static bool mq_send(const void *data, size_t len)
{
    if (!s_audio_topic[0]) build_audio_topic();
    mqtt_publish_binary(s_audio_topic, data, len);
    return true;
}

static void mq_send_marker(const char *json)
{
    if (!s_audio_topic[0]) build_audio_topic();
    mqtt_publish_text(s_audio_topic, json);
}

/* ------------------------------------------------------------------ */

static void mic_test_task_fn(void *arg)
{
    int duration_ms = (int)(uintptr_t)arg;
    if (duration_ms <= 0) duration_ms = MIC_TEST_DEFAULT_MS;
    if (duration_ms > MIC_TEST_MAX_MS) duration_ms = MIC_TEST_MAX_MS;

    int64_t start_us = esp_timer_get_time();
    int64_t deadline_us = start_us + (int64_t)duration_ms * 1000;

    static int16_t s_frame[MIC_TEST_CHUNK_SAMPLES];
    int chunk_count = 0;
    int total_samples = 0;

    ESP_LOGI(TAG, "Mic test start: %d ms (%d Hz, %d-bit mono)",
             duration_ms, I2S_CAPTURE_SAMPLE_RATE, 16);

#if CONFIG_ESPCLAW_DISPLAY_OLED || CONFIG_ESPCLAW_DISPLAY_TFT
    display_ui_set_state(DISPLAY_STATE_RECORDING);
#endif

    /* Note: sending raw PCM chunks only — backend rebuilds WAV header */

    while (s_capturing) {
        int64_t now_us = esp_timer_get_time();
        if (now_us >= deadline_us) break;

        /* Adjust timeout so we don't wait past deadline */
        int64_t remaining_ms = (deadline_us - now_us) / 1000;
        if (remaining_ms <= 0) break;
        TickType_t wait = remaining_ms < 50 ? pdMS_TO_TICKS(remaining_ms) : pdMS_TO_TICKS(50);

        if (!i2s_capture_is_ready()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        esp_err_t err = i2s_capture_read_frame(s_frame, MIC_TEST_CHUNK_SAMPLES, wait);
        if (err == ESP_ERR_TIMEOUT) continue;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "I2S read error: %s", esp_err_to_name(err));
            continue;
        }

        /* Process through AFE to get noise-suppressed clean audio */
        int16_t clean_frame[MIC_TEST_CHUNK_SAMPLES];
        size_t clean_samples = 0;
        bool vad_active = false;
        
        wakeword_process_audio_chunk(s_frame, MIC_TEST_CHUNK_SAMPLES,
                                     clean_frame, &clean_samples, &vad_active);

        if (clean_samples > 0) {
            mq_send(clean_frame, clean_samples * sizeof(int16_t));
            total_samples += clean_samples;
        } else {
            /* Fallback: Publish raw PCM chunk if AFE is not active */
            mq_send(s_frame, MIC_TEST_CHUNK_BYTES);
            total_samples += MIC_TEST_CHUNK_SAMPLES;
        }
        chunk_count++;
    }

    /* Send end marker */
    char marker[128];
    int written = snprintf(marker, sizeof(marker),
                           "{\"type\":\"mic_test_done\",\"chunks\":%d,\"samples\":%d}",
                           chunk_count, total_samples);
    mq_send_marker(marker);

    ESP_LOGI(TAG, "Mic test done: %d chunks, %d samples (%.1f s)",
             chunk_count, total_samples, total_samples / (float)I2S_CAPTURE_SAMPLE_RATE);

#if CONFIG_ESPCLAW_DISPLAY_OLED || CONFIG_ESPCLAW_DISPLAY_TFT
#if ESPCLAW_HAS_WAKEWORD
    if (wakeword_is_enabled()) {
        display_ui_set_state(DISPLAY_STATE_LISTENING);
    } else {
        display_ui_set_state(DISPLAY_STATE_READY);
    }
#else
    display_ui_set_state(DISPLAY_STATE_READY);
#endif
#endif

    s_capturing = false;
    s_mic_test_task = NULL;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */

esp_err_t mic_test_start(int duration_ms)
{
    if (s_capturing) {
        ESP_LOGW(TAG, "Already capturing");
        return ESP_ERR_INVALID_STATE;
    }

    if (!i2s_capture_is_ready()) {
        ESP_LOGI(TAG, "I2S not ready — initializing microphone on-the-fly...");
        i2s_capture_pin_cfg_t pins = {
            .gpio_ws  = CONFIG_ESPCLAW_MIC_I2S_WS,
            .gpio_sck = CONFIG_ESPCLAW_MIC_I2S_SCK,
            .gpio_sd  = CONFIG_ESPCLAW_MIC_I2S_SD,
        };
        int32_t v;
        if (nvs_mgr_get_i32("gpio_mic_ws", &v)) {
            pins.gpio_ws = (int)v;
        }
        if (nvs_mgr_get_i32("gpio_mic_sck", &v)) {
            pins.gpio_sck = (int)v;
        }
        if (nvs_mgr_get_i32("gpio_mic_sd", &v)) {
            pins.gpio_sd = (int)v;
        }
        esp_err_t init_err = i2s_capture_init(&pins);
        if (init_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize microphone on-the-fly: %s", esp_err_to_name(init_err));
            return init_err;
        }
    }

    build_audio_topic();
    s_capturing = true;

    BaseType_t ok = xTaskCreate(
        mic_test_task_fn,
        "mic_test",
        4096,
        (void *)(uintptr_t)(duration_ms > 0 ? duration_ms : MIC_TEST_DEFAULT_MS),
        5,
        &s_mic_test_task
    );

    if (ok != pdPASS) {
        s_capturing = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void mic_test_stop(void)
{
    s_capturing = false;
}

bool mic_test_is_capturing(void)
{
    return s_capturing;
}

/* Called from channel_mqtt.c MQTT event handler */
void mic_test_handle_command(const char *payload, size_t len)
{
    ESP_LOGI(TAG, "mic_test command received (len=%d): %.80s...", (int)len, payload);

    /* Stop */
    if (strstr(payload, "\"action\":\"stop\"")) {
        ESP_LOGI(TAG, "Mic test STOP");
        mic_test_stop();
        return;
    }

    /* Must have type + start */
    if (!strstr(payload, "\"type\":\"mic_test\"")) return;
    if (!strstr(payload, "\"action\":\"start\"")) return;

    /* Parse duration_ms robustly */
    int duration_ms = MIC_TEST_DEFAULT_MS;
    const char *d = strstr(payload, "\"duration_ms\"");
    if (d) {
        /* Find the colon, then skip whitespace, then parse digits */
        const char *colon = strchr(d, ':');
        if (colon) {
            const char *p = colon + 1;
            while (*p == ' ' || *p == '\t' || *p == '\n') p++;
            int val = atoi(p);
            if (val >= 100 && val <= MIC_TEST_MAX_MS) {
                duration_ms = val;
                ESP_LOGI(TAG, "Parsed duration_ms=%d", duration_ms);
            } else {
                ESP_LOGW(TAG, "duration_ms out of range: %d (using default %d)", val, duration_ms);
            }
        }
    }

    ESP_LOGI(TAG, "Mic test START: %d ms, I2S ready=%d",
             duration_ms, i2s_capture_is_ready());

    esp_err_t err = mic_test_start(duration_ms);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mic_test_start failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "mic_test task spawned OK");
    }
}
