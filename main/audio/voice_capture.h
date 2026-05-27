/*
 * ESPClaw - audio/voice_capture.h
 * Capture voice command after wake word detection.
 * Records PCM 16kHz mono into a PSRAM buffer with simple VAD.
 */
#ifndef VOICE_CAPTURE_H
#define VOICE_CAPTURE_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Default capture parameters */
#define VOICE_CAPTURE_MAX_MS        3000    /* Max recording duration */
#define VOICE_CAPTURE_SAMPLE_RATE   16000
#define VOICE_CAPTURE_MAX_SAMPLES   (VOICE_CAPTURE_SAMPLE_RATE * VOICE_CAPTURE_MAX_MS / 1000)
#define VOICE_CAPTURE_SILENCE_MS    800     /* Stop after this much silence */
#define VOICE_CAPTURE_SILENCE_RMS   200     /* RMS below this = silence */
#define VOICE_CAPTURE_MIN_VOICE_MS  300     /* Min voice before allowing silence stop */

/* Result of a voice capture session */
typedef struct {
    int16_t *samples;       /* PCM buffer (caller must free, allocated in PSRAM) */
    size_t   num_samples;   /* Number of valid samples captured */
    bool     vad_stopped;   /* true if stopped by silence detection */
} voice_capture_result_t;

/**
 * Capture voice from I2S mic into a PSRAM buffer.
 *
 * Blocks the calling task for up to max_ms milliseconds.
 * Uses simple RMS-based VAD: stops early if silence detected
 * for VOICE_CAPTURE_SILENCE_MS after initial voice activity.
 *
 * @param max_ms    Maximum recording duration (0 = use default 3000ms)
 * @param result    Output: captured audio data
 * @return ESP_OK on success, ESP_ERR_NO_MEM if PSRAM alloc fails
 */
esp_err_t voice_capture_record(int max_ms, voice_capture_result_t *result);

/**
 * Free a voice capture result buffer.
 */
void voice_capture_free(voice_capture_result_t *result);

#endif /* VOICE_CAPTURE_H */
