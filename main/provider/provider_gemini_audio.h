/*
 * ESPClaw - provider/provider_gemini_audio.h
 * Gemini multimodal audio API: send PCM audio directly to Gemini
 * for speech understanding + LLM response in one API call.
 */
#ifndef PROVIDER_GEMINI_AUDIO_H
#define PROVIDER_GEMINI_AUDIO_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/**
 * Send PCM audio to Gemini multimodal API and get text response.
 *
 * Encodes PCM -> WAV -> base64 -> sends to Gemini generateContent API.
 * The API handles STT + LLM reasoning in a single call.
 *
 * @param pcm_samples    PCM 16-bit mono samples
 * @param num_samples    Number of samples
 * @param system_prompt  System prompt (may be NULL)
 * @param response_buf   Output buffer for text response
 * @param response_sz    Size of response buffer
 * @return ESP_OK on success
 */
esp_err_t gemini_audio_complete(
    const int16_t *pcm_samples,
    size_t         num_samples,
    const char    *system_prompt,
    char          *response_buf,
    size_t         response_sz);

#endif /* PROVIDER_GEMINI_AUDIO_H */
