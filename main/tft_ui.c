/*
 * ESPClaw - tft_ui.c
 * ILI9341 240x320 SPI display for ESP32-S3 N16R8 + 2.4" TFT module.
 *
 * Default pin map (ESP32-S3 DevKit + common 2.4" SPI TFT shield):
 *   Display: SDI/MOSI=11, SDO/MISO=13, SCK=12, CS=10, DC=9, RESET=8, LED/BL=38
 *   Touch:   T_DIN=11, T_DO=13, T_CLK=12, T_CS=14, T_IRQ=21 (shared SPI bus)
 */
#include "tft_ui.h"
#include "platform.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"
#include "wifi_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "tft_ui";

#define TFT_H_RES       CONFIG_ESPCLAW_TFT_WIDTH
#define TFT_V_RES       CONFIG_ESPCLAW_TFT_HEIGHT
#define TFT_SPI_HOST    SPI2_HOST

#define COLOR_BG        0x1082   /* dark gray RGB565 */
#define COLOR_FG        0xFFFF
#define COLOR_ACCENT    0x4A69

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;
static bool s_initialized = false;
static char s_device_id[16] = {0};

/* Scaled 2x from oled 5x7 font → ~10x14 px per character */
static const uint8_t FONT5X7[][5] = {
    {0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},
    {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},
    {0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},
    {0x14,0x08,0x3E,0x08,0x14},
    {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},
    {0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},
    {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},
    {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},
    {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},
    {0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},
    {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},
    {0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43},
};

static void backlight_set(bool on)
{
    gpio_set_level(CONFIG_ESPCLAW_TFT_PIN_BL, on ? 1 : 0);
}

static esp_err_t fill_screen(uint16_t color)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;

    size_t line_pixels = TFT_H_RES;
    uint16_t *line = ESPCLAW_MALLOC(line_pixels * sizeof(uint16_t));
    if (!line) return ESP_ERR_NO_MEM;

    for (size_t i = 0; i < line_pixels; i++) {
        line[i] = color;
    }

    for (int y = 0; y < TFT_V_RES; y++) {
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y, TFT_H_RES, y + 1, line);
        if (err != ESP_OK) {
            ESPCLAW_FREE(line);
            return err;
        }
        if ((y & 0x0F) == 0) {
            vTaskDelay(1);
        }
    }
    ESPCLAW_FREE(line);
    return ESP_OK;
}

static void draw_char_scaled(uint16_t *line_buf, int x, int y, char c)
{
    if (c < 0x20 || c > 0x5A) c = 0x20;
    if (c >= 0x5B && c <= 0x60) c = 0x20;
    if (c >= 0x7B) c = 0x20;

    const uint8_t *g = FONT5X7[(uint8_t)c - 0x20];
    const int scale = 2;
    const int char_w = 6 * scale;
    const int char_h = 8 * scale;

    for (int col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < 8; row++) {
            if (!(bits & (1 << row))) continue;
            for (int sy = 0; sy < scale; sy++) {
                int py = y + row * scale + sy;
                if (py < 0 || py >= TFT_V_RES) continue;
                for (int sx = 0; sx < scale; sx++) {
                    int px = x + col * scale + sx;
                    if (px < 0 || px >= TFT_H_RES) continue;
                    line_buf[py * TFT_H_RES + px] = COLOR_FG;
                }
            }
        }
    }
    (void)char_w;
    (void)char_h;
}

static void draw_text(int x, int y, const char *str)
{
    if (!s_panel || !str) return;

    const int line_h = 20;
    int cursor_x = x;

    uint16_t *chunk = ESPCLAW_MALLOC(TFT_H_RES * line_h * sizeof(uint16_t));
    if (!chunk) return;

    for (int row = 0; row < line_h; row++) {
        for (int col = 0; col < TFT_H_RES; col++) {
            chunk[row * TFT_H_RES + col] = COLOR_BG;
        }
    }

    while (*str) {
        if (cursor_x + 12 >= TFT_H_RES) break;
        draw_char_scaled(chunk, cursor_x, 0, *str++);
        cursor_x += 12;
    }

    esp_lcd_panel_draw_bitmap(s_panel, 0, y, TFT_H_RES, y + line_h, chunk);
    ESPCLAW_FREE(chunk);
}

static void render_screen(const char *l0, const char *l1, const char *l2,
                          const char *l3, const char *l4, const char *l5)
{
    fill_screen(COLOR_BG);
    if (l0) draw_text(8, 16, l0);
    if (l1) draw_text(8, 44, l1);
    if (l2) draw_text(8, 72, l2);
    if (l3) draw_text(8, 100, l3);
    if (l4) draw_text(8, 128, l4);
    if (l5) draw_text(8, 156, l5);
    if (s_device_id[0]) {
        draw_text(8, TFT_V_RES - 28, s_device_id);
    }
}

#if CONFIG_ESPCLAW_TFT_TOUCH_ENABLE
static esp_err_t touch_init(void)
{
    gpio_config_t irq_cfg = {
        .pin_bit_mask = 1ULL << CONFIG_ESPCLAW_TFT_PIN_T_IRQ,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&irq_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Touch IRQ GPIO config failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Touch XPT2046 pins: T_CS=%d T_IRQ=%d (shared SPI %d)",
             CONFIG_ESPCLAW_TFT_PIN_T_CS, CONFIG_ESPCLAW_TFT_PIN_T_IRQ,
             CONFIG_ESPCLAW_TFT_PIN_SCK);
    return ESP_OK;
}
#else
static esp_err_t touch_init(void) { return ESP_OK; }
#endif

esp_err_t tft_ui_init(void)
{
    if (s_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Init ILI9341 %dx%d SPI", TFT_H_RES, TFT_V_RES);

    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << CONFIG_ESPCLAW_TFT_PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bl_cfg));
    backlight_set(false);

    spi_bus_config_t buscfg = {
        .mosi_io_num = CONFIG_ESPCLAW_TFT_PIN_MOSI,
        .miso_io_num = CONFIG_ESPCLAW_TFT_PIN_MISO,
        .sclk_io_num = CONFIG_ESPCLAW_TFT_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_H_RES * 20 * sizeof(uint16_t),
    };
    esp_err_t err = spi_bus_initialize(TFT_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = CONFIG_ESPCLAW_TFT_PIN_CS,
        .dc_gpio_num = CONFIG_ESPCLAW_TFT_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = CONFIG_ESPCLAW_TFT_SPI_HZ * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_SPI_HOST, &io_config, &s_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel_io_spi failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = CONFIG_ESPCLAW_TFT_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_ili9341(s_io, &panel_config, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel_ili9341 failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    touch_init();

    backlight_set(true);
    s_initialized = true;

    render_screen("ESPClaw v0.2.0", "Initializing...", NULL, NULL, NULL, NULL);
    ESP_LOGI(TAG, "TFT ready (MOSI=%d MISO=%d SCK=%d CS=%d DC=%d RST=%d BL=%d)",
             CONFIG_ESPCLAW_TFT_PIN_MOSI, CONFIG_ESPCLAW_TFT_PIN_MISO,
             CONFIG_ESPCLAW_TFT_PIN_SCK, CONFIG_ESPCLAW_TFT_PIN_CS,
             CONFIG_ESPCLAW_TFT_PIN_DC, CONFIG_ESPCLAW_TFT_PIN_RST,
             CONFIG_ESPCLAW_TFT_PIN_BL);
    return ESP_OK;
}

void tft_ui_deinit(void)
{
    if (!s_initialized) return;
    backlight_set(false);
    if (s_panel) {
        esp_lcd_panel_disp_on_off(s_panel, false);
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_io) {
        esp_lcd_panel_io_del(s_io);
        s_io = NULL;
    }
    s_initialized = false;
}

void tft_ui_set_device_id(const char *id)
{
    if (!id) return;
    strncpy(s_device_id, id, sizeof(s_device_id) - 1);
    s_device_id[sizeof(s_device_id) - 1] = '\0';
}

void tft_ui_set_state(display_state_t state)
{
    if (!s_initialized) return;

    switch (state) {
        case DISPLAY_STATE_INIT:
            render_screen("ESPClaw v0.2.0", "Initializing...", NULL, NULL, NULL, NULL);
            break;
        case DISPLAY_STATE_WIFI_CONFIG:
            render_screen("== WiFi Config ==", "AP: ESPClaw-XXXXX", "PW: espclaw1",
                          "-> 192.168.4.1", NULL, NULL);
            break;
        case DISPLAY_STATE_CONNECTING:
            render_screen("ESPClaw v0.2.0", "WiFi connecting...", NULL, NULL, NULL, NULL);
            break;
        case DISPLAY_STATE_CONNECTED:
            render_screen("ESPClaw v0.2.0", "WiFi Connected!", NULL, NULL, NULL, NULL);
            break;
        case DISPLAY_STATE_LLM_CONFIG:
            render_screen("ESPClaw v0.2.0", "Set LLM key", "via web UI", NULL, NULL, NULL);
            break;
        case DISPLAY_STATE_READY: {
            char ip_tmp[16] = {0};
            if (wifi_mgr_get_ip_str(ip_tmp, sizeof(ip_tmp)) && ip_tmp[0]) {
                render_screen("ESPClaw v0.2.0", "** READY **", ip_tmp, NULL, NULL, NULL);
            } else {
                render_screen("ESPClaw v0.2.0", "** READY **", NULL, NULL, NULL, NULL);
            }
            break;
        }
        case DISPLAY_STATE_LISTENING:
            render_screen("ESPClaw v0.2.0", "** LISTEN **", "Wake word ON", NULL, NULL, NULL);
            break;
        case DISPLAY_STATE_WAKE:
            render_screen("ESPClaw v0.2.0", "** WAKE! **", "Agent...", NULL, NULL, NULL);
            break;
        case DISPLAY_STATE_ERROR:
            render_screen("== ERROR ==", "Check serial log", NULL, NULL, NULL, NULL);
            break;
        default:
            break;
    }
}

void tft_ui_update_ip(const char *ip)
{
    if (!s_initialized || !ip) return;
    render_screen("ESPClaw v0.2.0", "WiFi Connected!", ip, "** READY **", NULL, NULL);
}

void tft_ui_update_wifi(const char *ssid) { (void)ssid; }

void tft_ui_write(uint8_t col, uint8_t page, const char *str)
{
    if (!s_initialized || !str) return;
    int y = 16 + (int)page * 20;
    int x = 8 + (int)col * 6;
    draw_text(x, y, str);
}

void tft_ui_show_message(const char *line1, const char *line2, const char *line3)
{
    if (!s_initialized) return;
    render_screen(line1, line2, line3, NULL, NULL, NULL);
}

void tft_ui_clear(void)
{
    if (!s_initialized) return;
    fill_screen(COLOR_BG);
}

void tft_ui_update(void)
{
    /* TFT renders immediately; kept for API compatibility with OLED */
}
