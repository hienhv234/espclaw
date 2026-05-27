/*
 * ESPClaw - audio/voice_capture.c
 * Capture voice command from I2S mic into PSRAM buffer.
 *
 * Called by wakeword task after wake word is detected.
 * Reads I2S frames, stores PCM in PSRAM, uses simple RMS-based VAD
 * to detect end-of-speech.
 */
#include "voice_capture.h"
#include "i2s_capture.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wakeword.h"
#include <string.h>

static const char *TAG = "voice_cap";

/* RMS computation fallback if AFE is not providing VAD */
static uint32_t frame_rms(const int16_t *samples, size_t n)
{
    if (n == 0) return 0;
    uint64_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t v = samples[i];
        sum += (uint64_t)(v * v);
    }
    uint32_t mean = (uint32_t)(sum / n);
    if (mean == 0) return 0;
    uint32_t x = mean;
    x = (x + mean / x) >> 1;
    x = (x + mean / x) >> 1;
    x = (x + mean / x) >> 1;
    return x;
}

esp_err_t voice_capture_record(int max_ms, voice_capture_result_t *result)
{
    if (!result) return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));

    if (max_ms <= 0) max_ms = VOICE_CAPTURE_MAX_MS;
    if (max_ms > 10000) max_ms = 10000;

    if (!i2s_capture_is_ready()) {
        ESP_LOGE(TAG, "I2S mic not ready");
        return ESP_ERR_INVALID_STATE;
    }

    /* Calculate buffer size */
    size_t max_samples = (size_t)VOICE_CAPTURE_SAMPLE_RATE * max_ms / 1000;
    size_t buf_bytes = max_samples * sizeof(int16_t);

    /* Allocate in PSRAM */
    int16_t *buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes in PSRAM for voice capture", (unsigned)buf_bytes);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Voice capture start: max %d ms, buffer %u bytes",
             max_ms, (unsigned)buf_bytes);

    int16_t frame[I2S_CAPTURE_FRAME_SAMPLES];
    size_t total_samples = 0;
    int64_t start_us = esp_timer_get_time();
    int64_t deadline_us = start_us + (int64_t)max_ms * 1000;

    /* VAD state */
    bool got_voice = false;             /* Have we seen any voice activity? */
    int64_t silence_start_us = 0;       /* When silence started */
    int voice_frames = 0;               /* Count of frames with voice */
    bool vad_stopped = false;

    while (1) {
        int64_t now_us = esp_timer_get_time();
        if (now_us >= deadline_us) break;

        /* Read one frame from I2S */
        esp_err_t err = i2s_capture_read_frame(frame, I2S_CAPTURE_FRAME_SAMPLES,
                                                pdMS_TO_TICKS(100));
        if (err == ESP_ERR_TIMEOUT) continue;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "I2S read error: %s", esp_err_to_name(err));
            continue;
        }

        /* Process through AFE to get clean audio and VAD state */
        int16_t clean_frame[I2S_CAPTURE_FRAME_SAMPLES];
        size_t clean_samples = 0;
        bool vad_active = false;
        
        wakeword_process_audio_chunk(frame, I2S_CAPTURE_FRAME_SAMPLES,
                                     clean_frame, &clean_samples, &vad_active);

        /* Copy clean audio to buffer if space available */
        if (clean_samples > 0) {
            size_t copy_samples = clean_samples;
            if (total_samples + copy_samples > max_samples) {
                copy_samples = max_samples - total_samples;
            }
            if (copy_samples > 0) {
                memcpy(buf + total_samples, clean_frame, copy_samples * sizeof(int16_t));
                total_samples += copy_samples;
            }
        } else {
            // Fallback: If no clean chunk was produced, store raw
            size_t copy_samples = I2S_CAPTURE_FRAME_SAMPLES;
            if (total_samples + copy_samples > max_samples) {
                copy_samples = max_samples - total_samples;
            }
            if (copy_samples > 0) {
                memcpy(buf + total_samples, frame, copy_samples * sizeof(int16_t));
                total_samples += copy_samples;
            }
        }

        /* VAD logic */
        // Fallback RMS if vad_active is not set by AFE (e.g. using stub)
        if (!vad_active && clean_samples == 0) {
            uint32_t rms = frame_rms(frame, I2S_CAPTURE_FRAME_SAMPLES);
            vad_active = (rms >= VOICE_CAPTURE_SILENCE_RMS);
        }

        if (vad_active) {
            /* Voice activity */
            got_voice = true;
            voice_frames++;
            silence_start_us = 0;  /* reset silence timer */
        } else if (got_voice) {
            /* Silence after voice */
            if (silence_start_us == 0) {
                silence_start_us = now_us;
            }
            /* Check if we've had enough voice AND enough silence */
            int64_t voice_duration_ms = (now_us - start_us) / 1000;
            int64_t silence_duration_ms = (now_us - silence_start_us) / 1000;

            if (voice_duration_ms >= VOICE_CAPTURE_MIN_VOICE_MS &&
                silence_duration_ms >= VOICE_CAPTURE_SILENCE_MS) {
                ESP_LOGI(TAG, "VAD: silence detected after %lld ms voice, stopping",
                         (long long)voice_duration_ms);
                vad_stopped = true;
                break;
            }
        }

        /* Buffer full */
        if (total_samples >= max_samples) break;
    }

    float duration_s = (float)total_samples / VOICE_CAPTURE_SAMPLE_RATE;
    ESP_LOGI(TAG, "Voice capture done: %u samples (%.1f s), voice_frames=%d, vad_stop=%d",
             (unsigned)total_samples, duration_s, voice_frames, vad_stopped);

    if (total_samples == 0) {
        heap_caps_free(buf);
        ESP_LOGW(TAG, "No audio captured");
        return ESP_ERR_NOT_FOUND;
    }

    result->samples = buf;
    result->num_samples = total_samples;
    result->vad_stopped = vad_stopped;
    return ESP_OK;
}

void voice_capture_free(voice_capture_result_t *result)
{
    if (result && result->samples) {
        heap_caps_free(result->samples);
        result->samples = NULL;
        result->num_samples = 0;
    }
}
