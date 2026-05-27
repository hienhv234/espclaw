/*
 * ESPClaw - audio/mic_test.h
 */
#ifndef MIC_TEST_H
#define MIC_TEST_H

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

/*
 * Start microphone capture and stream PCM chunks to MQTT espclaw/{id}/audio.
 * Duration in milliseconds (1.8 s default, max 10 s).
 */
esp_err_t mic_test_start(int duration_ms);

void mic_test_stop(void);

bool mic_test_is_capturing(void);

/*
 * Parse mic test command from MQTT JSON payload (called by channel_mqtt.c).
 * Looks for {"type":"mic_test","action":"start"|"stop",...}
 */
void mic_test_handle_command(const char *payload, size_t len);

#endif /* MIC_TEST_H */
