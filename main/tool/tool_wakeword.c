/*
 * ESPClaw - tool/tool_wakeword.c
 */
#include "wakeword/wakeword.h"
#include "platform.h"
#include "util/json_util.h"
#include <stdio.h>
#include <string.h>

#if ESPCLAW_HAS_WAKEWORD

static bool tool_wakeword_enable(const char *input_json, char *result_buf, size_t result_sz)
{
    int en = -1;
    if (json_get_int(input_json, "enabled", &en)) {
        wakeword_set_enabled(en != 0);
    }
    snprintf(result_buf, result_sz, "Wake word listen: %s",
             wakeword_is_enabled() ? "ON" : "OFF");
    return true;
}

static bool tool_wakeword_status(const char *input_json, char *result_buf, size_t result_sz)
{
    (void)input_json;
    snprintf(result_buf, result_sz,
             "enabled=%s threshold=%.0f train_state=%d",
             wakeword_is_enabled() ? "true" : "false",
             wakeword_get_threshold(),
             (int)wakeword_train_get_state());
    return true;
}

static bool tool_wakeword_set_threshold(const char *input_json, char *result_buf, size_t result_sz)
{
    int thr = 0;
    if (!json_get_int(input_json, "threshold", &thr)) {
        snprintf(result_buf, result_sz, "Error: 'threshold' required (integer)");
        return false;
    }
    wakeword_set_threshold((float)thr);
    snprintf(result_buf, result_sz, "Threshold set to %d", thr);
    return true;
}

static bool tool_wakeword_train_start(const char *input_json, char *result_buf, size_t result_sz)
{
    char label[32] = "custom";
    json_get_str(input_json, "label", label, sizeof(label));
    esp_err_t err = wakeword_train_start(label);
    if (err != ESP_OK) {
        snprintf(result_buf, result_sz, "Error: %s", esp_err_to_name(err));
        return false;
    }
    snprintf(result_buf, result_sz, "Training started for '%s'. Use wakeword_train_record.", label);
    return true;
}

static bool tool_wakeword_train_record(const char *input_json, char *result_buf, size_t result_sz)
{
    bool positive = true;
    char type[16] = {0};
    if (json_get_str(input_json, "type", type, sizeof(type))) {
        positive = (strcmp(type, "neg") != 0 && strcmp(type, "negative") != 0);
    }
    esp_err_t err = wakeword_train_record(positive);
    if (err == ESP_OK) {
        snprintf(result_buf, result_sz, "Recorded %s sample",
                 positive ? "positive" : "negative");
    } else {
        snprintf(result_buf, result_sz, "Error: %s",
                 esp_err_to_name(err));
    }
    return err == ESP_OK;
}

static bool tool_wakeword_train_finish(const char *input_json, char *result_buf, size_t result_sz)
{
    (void)input_json;
    esp_err_t err = wakeword_train_finish();
    if (err == ESP_ERR_NOT_SUPPORTED) {
        snprintf(result_buf, result_sz,
                 "Samples counted; cloud upload/train pending (Phase 2)");
        return true;
    }
    snprintf(result_buf, result_sz, err == ESP_OK ? "Training pipeline done" : "Error: %s",
             esp_err_to_name(err));
    return err == ESP_OK;
}

#else /* !ESPCLAW_HAS_WAKEWORD */

static bool ww_unsupported(const char *input_json, char *result_buf, size_t result_sz)
{
    (void)input_json;
    snprintf(result_buf, result_sz, "Wake word requires ESP32-S3 with PSRAM");
    return false;
}

#define tool_wakeword_enable           ww_unsupported
#define tool_wakeword_status           ww_unsupported
#define tool_wakeword_set_threshold    ww_unsupported
#define tool_wakeword_train_start      ww_unsupported
#define tool_wakeword_train_record     ww_unsupported
#define tool_wakeword_train_finish     ww_unsupported

#endif /* ESPCLAW_HAS_WAKEWORD */
