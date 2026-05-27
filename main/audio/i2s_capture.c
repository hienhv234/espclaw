/*
 * ESPClaw - audio/i2s_capture.c
 * Standard I2S RX for digital MEMS mic (INMP441 class).
 */
#include "i2s_capture.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "i2s_cap";

static i2s_chan_handle_t s_rx_chan;
static bool s_ready;
static uint32_t s_last_rms;

static uint32_t pcm_rms(const int16_t *samples, size_t n)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t v = samples[i];
        sum += (uint64_t)(v * v);
    }
    if (n == 0) {
        return 0;
    }
    return (uint32_t)(sum / n);
}

esp_err_t i2s_capture_init(const i2s_capture_pin_cfg_t *pins)
{
    if (!pins || pins->gpio_ws < 0 || pins->gpio_sck < 0 || pins->gpio_sd < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ready) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 6;
    chan_cfg.dma_frame_num = 240;

    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(I2S_CAPTURE_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = pins->gpio_sck,
            .ws   = pins->gpio_ws,
            .dout = I2S_GPIO_UNUSED,
            .din  = pins->gpio_sd,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init_std_mode: %s", esp_err_to_name(err));
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        return err;
    }

    err = i2s_channel_enable(s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "channel_enable: %s", esp_err_to_name(err));
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "I2S mic ready (ws=%d sck=%d sd=%d @%dHz)",
             pins->gpio_ws, pins->gpio_sck, pins->gpio_sd, I2S_CAPTURE_SAMPLE_RATE);
    return ESP_OK;
}

void i2s_capture_deinit(void)
{
    if (s_rx_chan) {
        i2s_channel_disable(s_rx_chan);
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
    }
    s_ready = false;
    s_last_rms = 0;
}

bool i2s_capture_is_ready(void)
{
    return s_ready;
}

esp_err_t i2s_capture_read_frame(int16_t *out, size_t samples, TickType_t timeout)
{
    if (!s_ready || !out || samples == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytes_read = 0;
    size_t want_bytes = samples * sizeof(int16_t);
    esp_err_t err = i2s_channel_read(s_rx_chan, out, want_bytes, &bytes_read, timeout);
    if (err != ESP_OK) {
        return err;
    }
    size_t got_samples = bytes_read / sizeof(int16_t);
    if (got_samples < samples) {
        memset(out + got_samples, 0, (samples - got_samples) * sizeof(int16_t));
    }
    s_last_rms = pcm_rms(out, samples);
    return ESP_OK;
}

uint32_t i2s_capture_last_rms(void)
{
    return s_last_rms;
}
