/*
 * ESPClaw - audio/i2s_capture.h
 * I2S microphone capture (16 kHz mono PCM). Static buffers only.
 */
#ifndef I2S_CAPTURE_H
#define I2S_CAPTURE_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define I2S_CAPTURE_SAMPLE_RATE   16000
#define I2S_CAPTURE_FRAME_SAMPLES 512

typedef struct {
    int gpio_ws;
    int gpio_sck;
    int gpio_sd;
} i2s_capture_pin_cfg_t;

esp_err_t i2s_capture_init(const i2s_capture_pin_cfg_t *pins);
void i2s_capture_deinit(void);
bool i2s_capture_is_ready(void);

/* Read one frame (512 samples). Returns ESP_OK or timeout/error. */
esp_err_t i2s_capture_read_frame(int16_t *out, size_t samples, TickType_t timeout);

/* RMS of last frame (0 if not ready). */
uint32_t i2s_capture_last_rms(void);

#endif /* I2S_CAPTURE_H */
