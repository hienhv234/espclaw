/*
 * ESPClaw - wakeword/wakeword_engine_stub.c
 * MVP stub: energy (RMS) detector for pipeline testing.
 * Replace with esp-sr WakeNet or TFLite in Phase 1.9 / Phase 3.
 */
#include "wakeword_ops.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "ww_stub";

static float s_threshold = 1200.0f;
static int s_hot_frames;
static const int HOT_FRAMES_NEED = 4;

static esp_err_t stub_init(void)
{
    s_hot_frames = 0;
    ESP_LOGI(TAG, "Stub engine ready (RMS threshold=%.0f)", s_threshold);
    return ESP_OK;
}

static void stub_deinit(void)
{
    s_hot_frames = 0;
}

static bool stub_feed(const int16_t *pcm, size_t samples, float *score_out)
{
    if (!pcm || samples == 0) {
        return false;
    }
    uint64_t sum = 0;
    for (size_t i = 0; i < samples; i++) {
        int32_t v = pcm[i];
        sum += (uint64_t)(v * v);
    }
    float rms = (float)sqrt((double)(sum / samples));
    if (score_out) {
        *score_out = rms;
    }

    if (rms >= s_threshold) {
        s_hot_frames++;
    } else {
        s_hot_frames = 0;
    }

    if (s_hot_frames >= HOT_FRAMES_NEED) {
        s_hot_frames = 0;
        ESP_LOGI(TAG, "Stub wake (rms=%.0f >= %.0f)", rms, s_threshold);
        return true;
    }
    return false;
}

static esp_err_t stub_set_threshold(float threshold)
{
    if (threshold < 100.0f) {
        threshold = 100.0f;
    }
    if (threshold > 20000.0f) {
        threshold = 20000.0f;
    }
    s_threshold = threshold;
    return ESP_OK;
}

static esp_err_t stub_set_model_path(const char *path)
{
    (void)path;
    return ESP_ERR_NOT_SUPPORTED;
}

static const wakeword_ops_t s_stub_ops = {
    .name            = "rms_stub",
    .init            = stub_init,
    .deinit          = stub_deinit,
    .feed            = stub_feed,
    .set_threshold   = stub_set_threshold,
    .set_model_path  = stub_set_model_path,
};

const wakeword_ops_t *wakeword_ops_get_default(void)
{
    return &s_stub_ops;
}
