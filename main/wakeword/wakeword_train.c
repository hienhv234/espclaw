/*
 * ESPClaw - wakeword/wakeword_train.c
 * Training state machine (Phase 2 — skeleton).
 */
#include "wakeword.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ww_train";

static wakeword_train_state_t s_state = WW_TRAIN_IDLE;
static char s_label[32];
static uint16_t s_pos_count;
static uint16_t s_neg_count;

wakeword_train_state_t wakeword_train_get_state(void)
{
    return s_state;
}

esp_err_t wakeword_train_start(const char *label)
{
    if (!label || label[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_label, label, sizeof(s_label) - 1);
    s_label[sizeof(s_label) - 1] = '\0';
    s_pos_count = 0;
    s_neg_count = 0;
    s_state = WW_TRAIN_POS;
    ESP_LOGI(TAG, "Train start label='%s' — record positive samples", s_label);
    return ESP_OK;
}

esp_err_t wakeword_train_record(bool positive)
{
    if (s_state != WW_TRAIN_POS && s_state != WW_TRAIN_NEG) {
        return ESP_ERR_INVALID_STATE;
    }
    if (positive) {
        s_pos_count++;
        if (s_pos_count >= 20) {
            s_state = WW_TRAIN_NEG;
            ESP_LOGI(TAG, "Positive done (%u). Record negative samples.", s_pos_count);
        }
    } else {
        s_neg_count++;
    }
    ESP_LOGI(TAG, "Sample recorded pos=%u neg=%u", s_pos_count, s_neg_count);
    return ESP_OK;
}

esp_err_t wakeword_train_finish(void)
{
    if (s_state == WW_TRAIN_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_pos_count < 5) {
        ESP_LOGW(TAG, "Need more positive samples (have %u)", s_pos_count);
        return ESP_ERR_INVALID_STATE;
    }
    s_state = WW_TRAIN_UPLOAD;
    ESP_LOGI(TAG, "Train finish — upload/MQTT not implemented yet (Phase 2)");
    s_state = WW_TRAIN_IDLE;
    return ESP_ERR_NOT_SUPPORTED;
}
