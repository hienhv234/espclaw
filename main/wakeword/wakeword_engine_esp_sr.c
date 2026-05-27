/*
 * ESPClaw - wakeword/wakeword_engine_esp_sr.c
 * Implementation of wakeword_ops_t using espressif/esp-sr (AFE + WakeNet).
 */
#include "wakeword_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// esp-sr includes
#include "esp_afe_sr_models.h"
#include "esp_afe_config.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"
#include <string.h>

static const char *TAG = "ww_esp_sr";

static srmodel_list_t *s_models = NULL;
static const esp_afe_sr_iface_t *s_afe_handle = NULL;
static esp_afe_sr_data_t *s_afe_data = NULL;
static volatile bool s_is_init = false;

// We use a predefined threshold or get it from Kconfig. For now, 0.5f.
static float s_threshold = 0.5f;

static esp_err_t sr_init(void)
{
    if (s_is_init) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing esp-sr AFE and WakeNet...");

    // Get models from partition
    s_models = esp_srmodel_init("model");
    if (!s_models) {
        ESP_LOGE(TAG, "Failed to load models from 'model' partition. Did you flash srmodels.bin?");
        return ESP_FAIL;
    }

    // AFE_CONFIG_DEFAULT macro is not always available in new versions, use afe_config_init
    afe_config_t *afe_config = afe_config_init("M", s_models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (!afe_config) {
        ESP_LOGE(TAG, "Failed to init afe_config");
        return ESP_FAIL;
    }

    // We configure AFE for 1 mic, no reference (AEC off), NS on, VAD on, WakeNet on
    afe_config->aec_init = false;
    afe_config->se_init = false;
    afe_config->vad_init = true;
    afe_config->ns_init = true;
    afe_config->wakenet_init = true;
    afe_config->pcm_config.total_ch_num = 1;
    afe_config->pcm_config.mic_num = 1;
    afe_config->pcm_config.ref_num = 0;

    // Check and modify config if necessary to avoid conflicts
    afe_config = afe_config_check(afe_config);
    if (!afe_config) {
        ESP_LOGE(TAG, "AFE config check failed");
        return ESP_FAIL;
    }

    // Create AFE handle
    s_afe_handle = esp_afe_handle_from_config(afe_config);
    if (!s_afe_handle) {
        ESP_LOGE(TAG, "Failed to get AFE handle");
        afe_config_free(afe_config);
        return ESP_FAIL;
    }

    // Create AFE data
    s_afe_data = s_afe_handle->create_from_config(afe_config);
    if (!s_afe_data) {
        ESP_LOGE(TAG, "Failed to create AFE data instance");
        afe_config_free(afe_config);
        return ESP_FAIL;
    }

    // Set wakeword threshold if supported by AFE directly, or we let WakeNet handle it natively.
    // s_afe_handle->set_wakenet_params(s_afe_data, afe_config->wakenet_model_name, s_threshold);

    afe_config_free(afe_config);
    s_is_init = true;
    ESP_LOGI(TAG, "esp-sr engine initialized successfully");
    return ESP_OK;
}

static void sr_deinit(void)
{
    if (s_is_init && s_afe_handle && s_afe_data) {
        s_afe_handle->destroy(s_afe_data);
        s_afe_data = NULL;
        s_afe_handle = NULL;
        if (s_models) {
            esp_srmodel_deinit(s_models);
            s_models = NULL;
        }
        s_is_init = false;
        ESP_LOGI(TAG, "esp-sr engine de-initialized");
    }
}

static bool sr_feed(const int16_t *pcm, size_t samples, float *score_out)
{
    if (!s_is_init || !s_afe_handle || !s_afe_data) {
        return false;
    }

    // Feed chunk to AFE
    int afe_chunk_size = s_afe_handle->get_feed_chunksize(s_afe_data);
    
    // We expect the caller to feed exactly 'afe_chunk_size' (usually 16ms/512 bytes or 32ms/1024 bytes)
    // If not, caller must buffer. The wrapper in wakeword.c currently buffers.
    if (samples < afe_chunk_size) {
        return false; // Not enough data
    }

    // Feed to AFE
    s_afe_handle->feed(s_afe_data, pcm);

    // Fetch from AFE. This performs VAD and runs WakeNet.
    // It returns the state of WakeNet.
    afe_fetch_result_t *res = s_afe_handle->fetch(s_afe_data);
    if (!res || res->ret_value < 0 || res->data_size <= 0) {
        return false;
    }

    // AFE_FETCH_WWE_DETECTED indicates wake word was detected
    if (res->wake_word_index > 0) {
        ESP_LOGI(TAG, "WakeWord DETECTED! (index: %d)", res->wake_word_index);
        if (score_out) {
            *score_out = 1.0f; // Dummy score
        }
        return true;
    }

    return false;
}

static esp_err_t sr_set_threshold(float threshold)
{
    if (threshold < 0.1f) threshold = 0.1f;
    if (threshold > 0.99f) threshold = 0.99f;
    s_threshold = threshold;
    // Note: Applying to a running AFE might require restarting or a specific API.
    return ESP_OK;
}

static esp_err_t sr_set_model_path(const char *path)
{
    // esp-sr uses predefined models in partition.
    return ESP_ERR_NOT_SUPPORTED;
}

// Additional API exposed for voice_capture to get clean audio chunk
int sr_get_fetch_chunksize(void)
{
    if (s_is_init && s_afe_handle && s_afe_data) {
        return s_afe_handle->get_fetch_chunksize(s_afe_data);
    }
    return 0;
}

// Fetch a clean audio chunk for Gemini (after wakeword triggered)
afe_fetch_result_t *sr_fetch_clean_audio(void)
{
    if (s_is_init && s_afe_handle && s_afe_data) {
        return s_afe_handle->fetch(s_afe_data);
    }
    return NULL;
}

static bool sr_process_frame(const int16_t *pcm_in, size_t samples, 
                             int16_t *clean_out, size_t *clean_samples_out,
                             bool *vad_active, float *score_out)
{
    if (!s_is_init || !s_afe_handle || !s_afe_data) {
        if (clean_samples_out) *clean_samples_out = 0;
        if (vad_active) *vad_active = false;
        return false;
    }

    int afe_chunk_size = s_afe_handle->get_feed_chunksize(s_afe_data);
    if (samples < afe_chunk_size) {
        if (clean_samples_out) *clean_samples_out = 0;
        if (vad_active) *vad_active = false;
        return false;
    }

    s_afe_handle->feed(s_afe_data, pcm_in);

    afe_fetch_result_t *res = s_afe_handle->fetch(s_afe_data);
    if (!res || res->ret_value < 0 || res->data_size <= 0) {
        if (clean_samples_out) *clean_samples_out = 0;
        if (vad_active) *vad_active = false;
        return false;
    }

    if (clean_out && clean_samples_out) {
        int fetch_size = s_afe_handle->get_fetch_chunksize(s_afe_data);
        memcpy(clean_out, res->data, fetch_size * sizeof(int16_t));
        *clean_samples_out = fetch_size;
    }

    if (vad_active) {
        *vad_active = (res->vad_state == VAD_SPEECH);
    }

    if (res->wake_word_index > 0) {
        ESP_LOGI(TAG, "WakeWord DETECTED! (index: %d)", res->wake_word_index);
        if (score_out) *score_out = 1.0f;
        return true;
    }

    return false;
}

static const wakeword_ops_t s_esp_sr_ops = {
    .name            = "esp_sr",
    .init            = sr_init,
    .deinit          = sr_deinit,
    .feed            = sr_feed,
    .process_frame   = sr_process_frame,
    .set_threshold   = sr_set_threshold,
    .set_model_path  = sr_set_model_path,
};

const wakeword_ops_t *wakeword_ops_get_default(void)
{
    return &s_esp_sr_ops;
}
