/*
 * ESPClaw - display_ui.c
 * Dispatches to OLED or TFT backend based on Kconfig.
 */
#include "display_ui.h"
#include "sdkconfig.h"

#if CONFIG_ESPCLAW_DISPLAY_TFT
#include "tft_ui.h"
#define BACKEND_INIT            tft_ui_init
#define BACKEND_DEINIT          tft_ui_deinit
#define BACKEND_SET_DEVICE_ID   tft_ui_set_device_id
#define BACKEND_SET_STATE       tft_ui_set_state
#define BACKEND_UPDATE_IP       tft_ui_update_ip
#define BACKEND_UPDATE_WIFI     tft_ui_update_wifi
#define BACKEND_WRITE           tft_ui_write
#define BACKEND_SHOW_MESSAGE    tft_ui_show_message
#define BACKEND_CLEAR           tft_ui_clear
#define BACKEND_UPDATE          tft_ui_update
#elif CONFIG_ESPCLAW_DISPLAY_OLED
#include "oled_ui.h"
#define BACKEND_INIT            oled_ui_init
#define BACKEND_DEINIT          oled_ui_deinit
#define BACKEND_SET_DEVICE_ID   oled_ui_set_device_id
#define BACKEND_SET_STATE(s)    oled_ui_set_state((oled_state_t)(s))
#define BACKEND_UPDATE_IP       oled_ui_update_ip
#define BACKEND_UPDATE_WIFI     oled_ui_update_wifi
#define BACKEND_WRITE           oled_ui_write
#define BACKEND_SHOW_MESSAGE    oled_ui_show_message
#define BACKEND_CLEAR           oled_ui_clear
#define BACKEND_UPDATE          oled_ui_update
#else
#define BACKEND_INIT()                  ESP_ERR_NOT_SUPPORTED
#define BACKEND_DEINIT()                ((void)0)
#define BACKEND_SET_DEVICE_ID(id)       ((void)(id))
#define BACKEND_SET_STATE(s)            ((void)(s))
#define BACKEND_UPDATE_IP(ip)           ((void)(ip))
#define BACKEND_UPDATE_WIFI(ssid)       ((void)(ssid))
#define BACKEND_WRITE(c, p, s)          ((void)(c),(void)(p),(void)(s))
#define BACKEND_SHOW_MESSAGE(a,b,c)     ((void)(a),(void)(b),(void)(c))
#define BACKEND_CLEAR()                 ((void)0)
#define BACKEND_UPDATE()                ((void)0)
#endif

esp_err_t display_ui_init(void)
{
    return BACKEND_INIT();
}

void display_ui_deinit(void)
{
    BACKEND_DEINIT();
}

void display_ui_set_device_id(const char *id)
{
    BACKEND_SET_DEVICE_ID(id);
}

extern void tool_led_set_status(const char *effect, int r, int g, int b, int delay_ms);

void display_ui_set_state(display_state_t state)
{
    BACKEND_SET_STATE(state);

    /* Update RGB LED status */
    switch (state) {
        case DISPLAY_STATE_INIT:
            tool_led_set_status("solid", 0, 50, 50, 0); // Dim cyan
            break;
        case DISPLAY_STATE_WIFI_CONFIG:
            tool_led_set_status("blink", 0, 0, 255, 500); // Blink blue
            break;
        case DISPLAY_STATE_CONNECTING:
            tool_led_set_status("blink", 255, 100, 0, 300); // Blink orange
            break;
        case DISPLAY_STATE_CONNECTED:
            tool_led_set_status("solid", 0, 255, 0, 0); // Solid green
            break;
        case DISPLAY_STATE_LLM_CONFIG:
            tool_led_set_status("blink", 128, 0, 128, 500); // Blink purple
            break;
        case DISPLAY_STATE_READY:
            tool_led_set_status("off", 0, 0, 0, 0); // Off when idle
            break;
        case DISPLAY_STATE_WAKE:
            tool_led_set_status("solid", 0, 100, 255, 0); // Solid bright blue
            break;
        case DISPLAY_STATE_RECORDING:
            tool_led_set_status("blink", 255, 0, 0, 200); // Fast blink red
            break;
        case DISPLAY_STATE_THINKING:
            tool_led_set_status("blink", 255, 200, 0, 400); // Blink yellow
            break;
        case DISPLAY_STATE_LISTENING:
            tool_led_set_status("solid", 0, 255, 100, 0); // Solid teal
            break;
        case DISPLAY_STATE_ERROR:
            tool_led_set_status("solid", 255, 0, 0, 0); // Solid red
            break;
        default:
            break;
    }
}

void display_ui_update_ip(const char *ip)
{
    BACKEND_UPDATE_IP(ip);
}

void display_ui_update_wifi(const char *ssid)
{
    BACKEND_UPDATE_WIFI(ssid);
}

void display_ui_write(uint8_t col, uint8_t page, const char *str)
{
    BACKEND_WRITE(col, page, str);
}

void display_ui_show_message(const char *line1, const char *line2, const char *line3)
{
    BACKEND_SHOW_MESSAGE(line1, line2, line3);
}

void display_ui_clear(void)
{
    BACKEND_CLEAR();
}

void display_ui_update(void)
{
    BACKEND_UPDATE();
}
