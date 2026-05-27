/*
 * ESPClaw - wakeword/wakeword.h
 * Public API for wake word listen / train / agent bridge.
 */
#ifndef WAKEWORD_H
#define WAKEWORD_H

#include "bus/message_bus.h"
#include "esp_err.h"
#include <stdbool.h>

#define WAKEWORD_AGENT_PROMPT \
    "[wake word] Nguoi dung vua goi tro ly bang giong noi. Chao ngan va hoi can giup gi."

esp_err_t wakeword_init(message_bus_t *bus);
esp_err_t wakeword_start(void);
void wakeword_stop(void);

bool wakeword_is_enabled(void);
esp_err_t wakeword_set_enabled(bool enabled);

float wakeword_get_threshold(void);
esp_err_t wakeword_set_threshold(float threshold);

/* Pause listening while agent uses LLM (avoid CPU/RAM contention). */
void wakeword_set_agent_busy(bool busy);
bool wakeword_is_agent_busy(void);

esp_err_t wakeword_post_to_agent(void);

/* Training API (Phase 2 skeleton) */
typedef enum {
    WW_TRAIN_IDLE = 0,
    WW_TRAIN_POS,
    WW_TRAIN_NEG,
    WW_TRAIN_UPLOAD,
} wakeword_train_state_t;

wakeword_train_state_t wakeword_train_get_state(void);
esp_err_t wakeword_train_start(const char *label);
esp_err_t wakeword_train_record(bool positive);
esp_err_t wakeword_train_finish(void);

/**
 * Handle MQTT JSON on espclaw/{id}/cmd with type=wakeword.
 * Returns true if consumed (do not forward to agent).
 */
bool wakeword_handle_mqtt_json(const char *payload);

/**
 * @brief Process one audio frame, returning clean audio and VAD state
 * @return true if wake word detected in this frame
 */
bool wakeword_process_audio_chunk(const int16_t *pcm_in, size_t samples, 
                                  int16_t *clean_out, size_t *clean_samples_out,
                                  bool *vad_active);

#endif /* WAKEWORD_H */
