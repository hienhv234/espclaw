/*
 * ESPClaw - wakeword/wakeword.c
 */
#include "wakeword.h"
#include "wakeword_ops.h"
#include "audio/i2s_capture.h"
#include "audio/mic_test.h"
#include "audio/voice_capture.h"
#include "messages.h"
#include "mem/nvs_manager.h"
#include "nvs_keys.h"
#include "platform.h"
#include "sdkconfig.h"
#include "display_ui.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "wakeword";

static message_bus_t *s_bus;
static TaskHandle_t s_task;
static const wakeword_ops_t *s_ops;
static bool s_enabled;
static bool s_agent_busy;
static int64_t s_last_wake_ms;

#define WAKE_COOLDOWN_MS       3000
#define WAKE_TASK_STACK        4096
#define WAKE_TASK_PRIO         4

static i2s_capture_pin_cfg_t load_mic_pins(void)
{
    i2s_capture_pin_cfg_t p = {
        .gpio_ws  = CONFIG_ESPCLAW_MIC_I2S_WS,
        .gpio_sck = CONFIG_ESPCLAW_MIC_I2S_SCK,
        .gpio_sd  = CONFIG_ESPCLAW_MIC_I2S_SD,
    };
    int32_t v;
    if (nvs_mgr_get_i32("gpio_mic_ws", &v)) {
        p.gpio_ws = (int)v;
    }
    if (nvs_mgr_get_i32("gpio_mic_sck", &v)) {
        p.gpio_sck = (int)v;
    }
    if (nvs_mgr_get_i32("gpio_mic_sd", &v)) {
        p.gpio_sd = (int)v;
    }
    return p;
}

static void load_nvs_config(void)
{
    int32_t en = 0;
    int32_t thr = 1200;
    if (nvs_mgr_get_i32(NVS_KEY_WAKEWORD_ENABLED, &en)) {
        s_enabled = (en != 0);
    } else {
        s_enabled = true;
    }
    if (nvs_mgr_get_i32(NVS_KEY_WAKEWORD_THRESHOLD, &thr)) {
        if (s_ops && s_ops->set_threshold) {
            s_ops->set_threshold((float)thr);
        }
    } else if (s_ops && s_ops->set_threshold) {
        s_ops->set_threshold((float)CONFIG_ESPCLAW_WAKEWORD_STUB_THRESHOLD);
    }
}

bool wakeword_is_enabled(void)
{
    return s_enabled;
}

esp_err_t wakeword_set_enabled(bool enabled)
{
    s_enabled = enabled;
    nvs_mgr_set_i32(NVS_KEY_WAKEWORD_ENABLED, enabled ? 1 : 0);
    return ESP_OK;
}

float wakeword_get_threshold(void)
{
    int32_t thr = 1200;
    nvs_mgr_get_i32(NVS_KEY_WAKEWORD_THRESHOLD, &thr);
    return (float)thr;
}

esp_err_t wakeword_set_threshold(float threshold)
{
    int32_t t = (int32_t)threshold;
    nvs_mgr_set_i32(NVS_KEY_WAKEWORD_THRESHOLD, t);
    if (s_ops && s_ops->set_threshold) {
        return s_ops->set_threshold(threshold);
    }
    return ESP_OK;
}

void wakeword_set_agent_busy(bool busy)
{
    s_agent_busy = busy;
}

bool wakeword_is_agent_busy(void)
{
    return s_agent_busy;
}

esp_err_t wakeword_post_to_agent(void)
{
    if (!s_bus) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_ESPCLAW_DISPLAY_OLED || CONFIG_ESPCLAW_DISPLAY_TFT
    display_ui_set_state(DISPLAY_STATE_WAKE);
#endif

    /* Capture voice command after wake word */
#if CONFIG_ESPCLAW_DISPLAY_OLED || CONFIG_ESPCLAW_DISPLAY_TFT
    display_ui_set_state(DISPLAY_STATE_RECORDING);
#endif

    voice_capture_result_t vcap = {0};
    esp_err_t err = voice_capture_record(VOICE_CAPTURE_MAX_MS, &vcap);

    inbound_msg_t msg = {0};
    msg.source  = MSG_SOURCE_WAKE;
    msg.chat_id = 0;

    if (err == ESP_OK && vcap.num_samples > 0) {
        /* Voice captured — send audio to agent */
        msg.audio_data    = vcap.samples;   /* PSRAM pointer, agent will free */
        msg.audio_samples = vcap.num_samples;
        snprintf(msg.text, sizeof(msg.text),
                 "[voice] %u samples (%.1f s)",
                 (unsigned)vcap.num_samples,
                 (float)vcap.num_samples / VOICE_CAPTURE_SAMPLE_RATE);
        ESP_LOGI(TAG, "Voice captured: %u samples -> agent", (unsigned)vcap.num_samples);
    } else {
        /* No voice captured — fall back to greeting prompt */
        strncpy(msg.text, WAKEWORD_AGENT_PROMPT, sizeof(msg.text) - 1);
        msg.audio_data    = NULL;
        msg.audio_samples = 0;
        ESP_LOGW(TAG, "No voice captured, using default prompt");
    }

    return message_bus_post_inbound(s_bus, &msg, pdMS_TO_TICKS(500));
}

static void wakeword_task_fn(void *arg)
{
    (void)arg;
    static int16_t s_frame[I2S_CAPTURE_FRAME_SAMPLES];

    while (1) {
        if (!s_enabled || s_agent_busy || mic_test_is_capturing()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (!i2s_capture_is_ready()) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (i2s_capture_read_frame(s_frame, I2S_CAPTURE_FRAME_SAMPLES,
                                   pdMS_TO_TICKS(100)) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        float score = 0.0f;
        if (s_ops && s_ops->process_frame) {
            bool detected = s_ops->process_frame(s_frame, I2S_CAPTURE_FRAME_SAMPLES, NULL, NULL, NULL, &score);
            if (detected) {
                int64_t now = (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                if (now - s_last_wake_ms >= WAKE_COOLDOWN_MS) {
                    s_last_wake_ms = now;
                    ESP_LOGI(TAG, "Wake detected (score=%.0f)", score);
                    wakeword_post_to_agent();
                }
            }
        } else if (s_ops && s_ops->feed && s_ops->feed(s_frame, I2S_CAPTURE_FRAME_SAMPLES, &score)) {
            // Fallback for older stubs
            int64_t now = (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            if (now - s_last_wake_ms >= WAKE_COOLDOWN_MS) {
                s_last_wake_ms = now;
                ESP_LOGI(TAG, "Wake detected (score=%.0f)", score);
                wakeword_post_to_agent();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

esp_err_t wakeword_init(message_bus_t *bus)
{
    s_bus = bus;
    s_ops = wakeword_ops_get_default();

    if (s_ops->init) {
        esp_err_t err = s_ops->init();
        if (err != ESP_OK) {
            return err;
        }
    }

    i2s_capture_pin_cfg_t pins = load_mic_pins();
    esp_err_t err = i2s_capture_init(&pins);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2S init failed: %s (wake word disabled)", esp_err_to_name(err));
        s_enabled = false;
        return err;
    }

    load_nvs_config();
    return ESP_OK;
}

esp_err_t wakeword_start(void)
{
    if (s_task) {
        return ESP_OK;
    }
    BaseType_t ok = ESPCLAW_CREATE_PINNED("wakeword", wakeword_task_fn, WAKE_TASK_STACK,
                                          NULL, WAKE_TASK_PRIO, &s_task, ESPCLAW_CORE_IO);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Listen task started (engine=%s, enabled=%d)",
             s_ops ? s_ops->name : "none", (int)s_enabled);
#if CONFIG_ESPCLAW_DISPLAY_OLED || CONFIG_ESPCLAW_DISPLAY_TFT
    if (s_enabled) {
        display_ui_set_state(DISPLAY_STATE_LISTENING);
    }
#endif
    return ESP_OK;
}

void wakeword_stop(void)
{
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_ops && s_ops->deinit) {
        s_ops->deinit();
    }
    i2s_capture_deinit();
}

static int preset_threshold(const char *preset)
{
    if (!preset) {
        return -1;
    }
    if (strcmp(preset, "hi_esp") == 0) {
        return 1200;
    }
    if (strcmp(preset, "hey_claw") == 0) {
        return 1100;
    }
    if (strcmp(preset, "hi_lexin") == 0) {
        return 1250;
    }
    if (strcmp(preset, "nihao") == 0) {
        return 1300;
    }
    return -1;
}

static void store_mode_and_keyword(const char *mode, const char *keyword, int model_ver)
{
    if (mode && mode[0]) {
        nvs_mgr_set_str(NVS_KEY_WAKEWORD_MODE, mode);
    }
    if (keyword && keyword[0]) {
        nvs_mgr_set_str(NVS_KEY_WAKEWORD_KEYWORD, keyword);
    }
    if (model_ver > 0) {
        nvs_mgr_set_i32(NVS_KEY_WAKEWORD_MODEL_VER, model_ver);
    }
}

static void apply_preset_nvs(const char *preset, const char *label)
{
    if (preset && preset[0]) {
        nvs_mgr_set_str(NVS_KEY_WAKEWORD_PRESET, preset);
    }
    if (label && label[0]) {
        nvs_mgr_set_str(NVS_KEY_WAKEWORD_LABEL, label);
    }
    int thr = preset_threshold(preset);
    if (thr > 0) {
        wakeword_set_threshold((float)thr);
    }
}

bool wakeword_handle_mqtt_json(const char *payload)
{
    if (!payload || payload[0] == '\0') {
        return false;
    }

    cJSON *root = cJSON_Parse(payload);
    if (!root) {
        return false;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "wakeword") != 0) {
        cJSON_Delete(root);
        return false;
    }

    cJSON *action = cJSON_GetObjectItem(root, "action");
    const char *act = cJSON_IsString(action) ? action->valuestring : "";

    if (strcmp(act, "enable") == 0 || strcmp(act, "sync") == 0) {
        cJSON *en = cJSON_GetObjectItem(root, "enabled");
        if (cJSON_IsBool(en)) {
            wakeword_set_enabled(cJSON_IsTrue(en));
        } else if (cJSON_IsNumber(en)) {
            wakeword_set_enabled(en->valueint != 0);
        }
    }

    if (strcmp(act, "sync") == 0 || strcmp(act, "set_threshold") == 0) {
        cJSON *thr = cJSON_GetObjectItem(root, "threshold");
        if (cJSON_IsNumber(thr)) {
            wakeword_set_threshold((float)thr->valuedouble);
        }
    }

    cJSON *src_mode = cJSON_GetObjectItem(root, "source_mode");
    const char *mode_str = cJSON_IsString(src_mode) ? src_mode->valuestring : NULL;

    cJSON *kw_item = cJSON_GetObjectItem(root, "keyword");
    const char *kw_str = cJSON_IsString(kw_item) ? kw_item->valuestring : NULL;

    cJSON *mv = cJSON_GetObjectItem(root, "model_version");
    int model_ver = cJSON_IsNumber(mv) ? mv->valueint : 0;

    if (strcmp(act, "sync") == 0) {
        if (mode_str && strcmp(mode_str, "keyword") == 0 && kw_str) {
            store_mode_and_keyword("keyword", kw_str, model_ver);
            cJSON *label = cJSON_GetObjectItem(root, "label");
            if (cJSON_IsString(label)) {
                nvs_mgr_set_str(NVS_KEY_WAKEWORD_LABEL, label->valuestring);
            }
        } else if (mode_str && strcmp(mode_str, "train") == 0) {
            store_mode_and_keyword("train", kw_str, model_ver);
            cJSON *label = cJSON_GetObjectItem(root, "label");
            if (cJSON_IsString(label)) {
                nvs_mgr_set_str(NVS_KEY_WAKEWORD_LABEL, label->valuestring);
            }
        } else if (mode_str) {
            store_mode_and_keyword(mode_str, NULL, model_ver);
        }
    }

    if (strcmp(act, "sync") == 0 || strcmp(act, "preset") == 0) {
        cJSON *preset = cJSON_GetObjectItem(root, "preset");
        cJSON *label  = cJSON_GetObjectItem(root, "label");
        const char *p = cJSON_IsString(preset) ? preset->valuestring : NULL;
        const char *l = cJSON_IsString(label) ? label->valuestring : NULL;
        if (p && p[0]) {
            apply_preset_nvs(p, l);
        } else if (l && l[0]) {
            nvs_mgr_set_str(NVS_KEY_WAKEWORD_LABEL, l);
        }
    }

    if (strcmp(act, "train_start") == 0) {
        cJSON *label = cJSON_GetObjectItem(root, "label");
        const char *l = cJSON_IsString(label) ? label->valuestring : "custom";
        wakeword_train_start(l);
    } else if (strcmp(act, "train_record") == 0) {
        cJSON *st = cJSON_GetObjectItem(root, "sample_type");
        bool pos = true;
        if (cJSON_IsString(st)) {
            pos = (strcmp(st->valuestring, "neg") != 0);
        }
        wakeword_train_record(pos);
    } else if (strcmp(act, "train_finish") == 0) {
        wakeword_train_finish();
    }

    if (strcmp(act, "sync") == 0) {
        ESP_LOGI(TAG, "MQTT sync: mode=%s enabled=%d thr=%.0f",
                 mode_str ? mode_str : "?",
                 (int)wakeword_is_enabled(), wakeword_get_threshold());
#if CONFIG_ESPCLAW_DISPLAY_OLED || CONFIG_ESPCLAW_DISPLAY_TFT
        if (wakeword_is_enabled()) {
            display_ui_set_state(DISPLAY_STATE_LISTENING);
        }
#endif
    }

    cJSON_Delete(root);
    return true;
}
bool wakeword_process_audio_chunk(const int16_t *pcm_in, size_t samples, 
                                  int16_t *clean_out, size_t *clean_samples_out,
                                  bool *vad_active)
{
    if (s_ops && s_ops->process_frame) {
        float dummy_score;
        return s_ops->process_frame(pcm_in, samples, clean_out, clean_samples_out, vad_active, &dummy_score);
    }
    // Fallback if engine doesn't support clean_out or VAD
    if (clean_out && clean_samples_out) {
        memcpy(clean_out, pcm_in, samples * sizeof(int16_t));
        *clean_samples_out = samples;
    }
    if (vad_active) {
        *vad_active = true; // Assume true
    }
    return false;
}
