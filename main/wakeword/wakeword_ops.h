/*
 * ESPClaw - wakeword/wakeword_ops.h
 * Wake word engine vtable (ESP-SR / TFLite / stub).
 */
#ifndef WAKEWORD_OPS_H
#define WAKEWORD_OPS_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    void (*deinit)(void);
    /* Feed one PCM frame; returns true if wake detected this frame. (Deprecated, use process_frame) */
    bool (*feed)(const int16_t *pcm, size_t samples, float *score_out);
    /* Process one PCM frame, optionally returning clean audio and VAD state. Returns true if wake detected. */
    bool (*process_frame)(const int16_t *pcm_in, size_t samples, 
                          int16_t *clean_out, size_t *clean_samples_out,
                          bool *vad_active, float *score_out);
    esp_err_t (*set_threshold)(float threshold);
    esp_err_t (*set_model_path)(const char *path);
} wakeword_ops_t;

const wakeword_ops_t *wakeword_ops_get_default(void);

#endif /* WAKEWORD_OPS_H */
