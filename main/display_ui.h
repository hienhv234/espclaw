/*
 * ESPClaw - display_ui.h
 * Unified display API: OLED (I2C) or TFT SPI 2.4" (240x320), selected via Kconfig.
 */
#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include "esp_err.h"

typedef enum {
    DISPLAY_STATE_INIT,
    DISPLAY_STATE_WIFI_CONFIG,
    DISPLAY_STATE_CONNECTING,
    DISPLAY_STATE_CONNECTED,
    DISPLAY_STATE_LLM_CONFIG,
    DISPLAY_STATE_READY,
    DISPLAY_STATE_LISTENING,
    DISPLAY_STATE_WAKE,
    DISPLAY_STATE_THINKING,
    DISPLAY_STATE_RECORDING,
    DISPLAY_STATE_ERROR
} display_state_t;

esp_err_t display_ui_init(void);
void display_ui_deinit(void);
void display_ui_set_device_id(const char *id);
void display_ui_set_state(display_state_t state);
void display_ui_update_ip(const char *ip);
void display_ui_update_wifi(const char *ssid);
void display_ui_write(uint8_t col, uint8_t page, const char *str);
void display_ui_show_message(const char *line1, const char *line2, const char *line3);
void display_ui_clear(void);
void display_ui_update(void);

#endif /* DISPLAY_UI_H */
