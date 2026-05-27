/*
 * ESPClaw - oled_ui.h
 * OLED display UI for ESPClaw main application
 */
#ifndef OLED_UI_H
#define OLED_UI_H

#include "esp_err.h"

#define OLED_SDA_GPIO  CONFIG_ESPCLAW_I2C_DEFAULT_SDA
#define OLED_SCL_GPIO  CONFIG_ESPCLAW_I2C_DEFAULT_SCL
#define OLED_I2C_ADDR  0x3C

typedef enum {
    OLED_STATE_INIT,
    OLED_STATE_WIFI_CONFIG,
    OLED_STATE_CONNECTING,
    OLED_STATE_CONNECTED,
    OLED_STATE_LLM_CONFIG,
    OLED_STATE_READY,
    OLED_STATE_LISTENING,
    OLED_STATE_WAKE,
    OLED_STATE_THINKING,
    OLED_STATE_RECORDING,
    OLED_STATE_ERROR
} oled_state_t;

esp_err_t oled_ui_init(void);
void oled_ui_deinit(void);
void oled_ui_set_device_id(const char *id);
void oled_ui_set_state(oled_state_t state);
void oled_ui_update_ip(const char *ip);
void oled_ui_update_wifi(const char *ssid);
void oled_ui_write(uint8_t col, uint8_t page, const char *str);
void oled_ui_show_message(const char *line1, const char *line2, const char *line3);
void oled_ui_clear(void);
void oled_ui_update(void);

#endif // OLED_UI_H
