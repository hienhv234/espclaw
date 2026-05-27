/*
 * ESPClaw - tft_ui.h
 * 2.4" SPI TFT 240x320 (ILI9341) + optional XPT2046 touch.
 */
#ifndef TFT_UI_H
#define TFT_UI_H

#include "display_ui.h"
#include "esp_err.h"

esp_err_t tft_ui_init(void);
void tft_ui_deinit(void);
void tft_ui_set_device_id(const char *id);
void tft_ui_set_state(display_state_t state);
void tft_ui_update_ip(const char *ip);
void tft_ui_update_wifi(const char *ssid);
void tft_ui_write(uint8_t col, uint8_t page, const char *str);
void tft_ui_show_message(const char *line1, const char *line2, const char *line3);
void tft_ui_clear(void);
void tft_ui_update(void);

#endif /* TFT_UI_H */
