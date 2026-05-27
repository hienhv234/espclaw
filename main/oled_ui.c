/*
 * ESPClaw - oled_ui.c
 * OLED SSD1306 128x64 driver - direct I2C (legacy API, reliable on IDF v5.3)
 *
 * Vì esp_lcd không support draw_bitmap cho monochrome 1-bit OLED,
 * chúng ta dùng legacy I2C với trình tự init đúng cho SSD1306 0.96"
 */
#include "oled_ui.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_manager.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "oled_ui";

#define OLED_I2C_PORT   I2C_NUM_0
#define OLED_I2C_FREQ   400000

#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_PAGES      (OLED_HEIGHT / 8)

/* SSD1306 commands */
#define SSD1306_DISPLAY_OFF       0xAE
#define SSD1306_DISPLAY_ON        0xAF
#define SSD1306_SET_CLOCK_DIV     0xD5
#define SSD1306_SET_MULTIPLEX     0xA8
#define SSD1306_SET_DISP_OFFSET   0xD3
#define SSD1306_SET_START_LINE    0x40
#define SSD1306_CHARGE_PUMP       0x8D
#define SSD1306_MEM_MODE          0x20
#define SSD1306_SEG_REMAP         0xA1  /* flip horizontal */
#define SSD1306_COM_SCAN_DEC      0xC8  /* flip vertical */
#define SSD1306_SET_COM_PINS      0xDA
#define SSD1306_SET_CONTRAST      0x81
#define SSD1306_SET_PRECHARGE     0xD9
#define SSD1306_SET_VCOMH         0xDB
#define SSD1306_DISPLAY_ALL_ON_RESUME 0xA4
#define SSD1306_NORMAL_DISPLAY    0xA6
#define SSD1306_SET_COL_ADDR      0x21
#define SSD1306_SET_PAGE_ADDR     0x22
#define SSD1306_NOP               0xE3

static uint8_t s_framebuffer[OLED_PAGES][OLED_WIDTH];
static bool s_initialized = false;
static char s_device_id[16] = {0};

/* Draw device ID on bottom of screen (pages 6-7) */
static void draw_device_id(void)
{
    if (s_device_id[0]) {
        oled_ui_write(0, 7, s_device_id);
    }
}

/* ── Low-level I2C helpers ────────────────────────────────────────────── */

static esp_err_t i2c_write_byte(uint8_t control, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, control, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(OLED_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t oled_cmd(uint8_t cmd)
{
    return i2c_write_byte(0x00, cmd);  /* Co=0, D/C#=0 → command */
}

static esp_err_t oled_cmd2(uint8_t cmd, uint8_t arg)
{
    esp_err_t ret = oled_cmd(cmd);
    if (ret == ESP_OK) ret = oled_cmd(arg);
    return ret;
}

/* ── Init sequence (SSD1306 application note + Adafruit reference) ─────── */

static esp_err_t ssd1306_init_sequence(void)
{
    /* Reset & power-on stabilization */
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t ret;
    ret = oled_cmd(SSD1306_DISPLAY_OFF);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "No ACK - check SDA/SCL/ADDR (0x%02X)", OLED_I2C_ADDR); return ret; }

    ret |= oled_cmd2(SSD1306_SET_CLOCK_DIV,    0x80);  /* clock / fosc */
    ret |= oled_cmd2(SSD1306_SET_MULTIPLEX,     0x3F);  /* 64 MUX */
    ret |= oled_cmd2(SSD1306_SET_DISP_OFFSET,   0x00);  /* no offset */
    ret |= oled_cmd(SSD1306_SET_START_LINE | 0x00);     /* start line 0 */
    ret |= oled_cmd2(SSD1306_CHARGE_PUMP,       0x14);  /* enable pump */
    ret |= oled_cmd2(SSD1306_MEM_MODE,          0x00);  /* horizontal mode */
    ret |= oled_cmd(SSD1306_SEG_REMAP);                 /* col 127 → SEG0 */
    ret |= oled_cmd(SSD1306_COM_SCAN_DEC);              /* scan from COM63 */
    ret |= oled_cmd2(SSD1306_SET_COM_PINS,      0x12);  /* alt, no remap */
    ret |= oled_cmd2(SSD1306_SET_CONTRAST,      0xCF);
    ret |= oled_cmd2(SSD1306_SET_PRECHARGE,     0xF1);
    ret |= oled_cmd2(SSD1306_SET_VCOMH,         0x40);
    ret |= oled_cmd(SSD1306_DISPLAY_ALL_ON_RESUME);     /* use RAM content */
    ret |= oled_cmd(SSD1306_NORMAL_DISPLAY);

    vTaskDelay(pdMS_TO_TICKS(10));
    ret |= oled_cmd(SSD1306_DISPLAY_ON);

    return ret;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

esp_err_t oled_ui_init(void)
{
    if (s_initialized) return ESP_OK;

    ESP_LOGI(TAG, "OLED init: SDA=%d SCL=%d ADDR=0x%02X",
             OLED_SDA_GPIO, OLED_SCL_GPIO, OLED_I2C_ADDR);

    /* Init I2C master */
    i2c_config_t conf = {
        .mode          = I2C_MODE_MASTER,
        .sda_io_num    = OLED_SDA_GPIO,
        .scl_io_num    = OLED_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = OLED_I2C_FREQ,
    };
    esp_err_t ret = i2c_param_config(OLED_I2C_PORT, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(OLED_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        /* ESP_ERR_INVALID_STATE = already installed, that's OK */
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = ssd1306_init_sequence();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SSD1306 init sequence failed");
        i2c_driver_delete(OLED_I2C_PORT);
        return ret;
    }

    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    s_initialized = true;
    ESP_LOGI(TAG, "OLED SSD1306 ready");

    oled_ui_write(0, 0, "ESPClaw v0.2.0");
    oled_ui_write(0, 1, "Initializing...");
    oled_ui_update();

    return ESP_OK;
}

void oled_ui_deinit(void)
{
    if (!s_initialized) return;
    oled_cmd(SSD1306_DISPLAY_OFF);
    i2c_driver_delete(OLED_I2C_PORT);
    s_initialized = false;
}

void oled_ui_update(void)
{
    if (!s_initialized) return;

    /* Set col 0-127, page 0-7 */
    oled_cmd2(SSD1306_SET_COL_ADDR,  0);    oled_cmd(127);
    oled_cmd2(SSD1306_SET_PAGE_ADDR, 0);    oled_cmd(7);

    /* Stream all pages in one I2C transaction */
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x40, true);  /* D/C#=1 → data */
    for (int p = 0; p < OLED_PAGES; p++) {
        i2c_master_write(cmd, s_framebuffer[p], OLED_WIDTH, true);
    }
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(OLED_I2C_PORT, cmd, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(cmd);
}

void oled_ui_clear(void)
{
    if (!s_initialized) return;
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

/* ── 5x7 font (printable ASCII 0x20–0x7E) ──────────────────────────────── */
static const uint8_t FONT5X7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 0x20 space
    {0x00,0x00,0x5F,0x00,0x00}, // 0x21 !
    {0x00,0x07,0x00,0x07,0x00}, // 0x22 "
    {0x14,0x7F,0x14,0x7F,0x14}, // 0x23 #
    {0x24,0x2A,0x7F,0x2A,0x12}, // 0x24 $
    {0x23,0x13,0x08,0x64,0x62}, // 0x25 %
    {0x36,0x49,0x55,0x22,0x50}, // 0x26 &
    {0x00,0x05,0x03,0x00,0x00}, // 0x27 '
    {0x00,0x1C,0x22,0x41,0x00}, // 0x28 (
    {0x00,0x41,0x22,0x1C,0x00}, // 0x29 )
    {0x14,0x08,0x3E,0x08,0x14}, // 0x2A *
    {0x08,0x08,0x3E,0x08,0x08}, // 0x2B +
    {0x00,0x50,0x30,0x00,0x00}, // 0x2C ,
    {0x08,0x08,0x08,0x08,0x08}, // 0x2D -
    {0x00,0x60,0x60,0x00,0x00}, // 0x2E .
    {0x20,0x10,0x08,0x04,0x02}, // 0x2F /
    {0x3E,0x51,0x49,0x45,0x3E}, // 0x30 0
    {0x00,0x42,0x7F,0x40,0x00}, // 0x31 1
    {0x42,0x61,0x51,0x49,0x46}, // 0x32 2
    {0x21,0x41,0x45,0x4B,0x31}, // 0x33 3
    {0x18,0x14,0x12,0x7F,0x10}, // 0x34 4
    {0x27,0x45,0x45,0x45,0x39}, // 0x35 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 0x36 6
    {0x01,0x71,0x09,0x05,0x03}, // 0x37 7
    {0x36,0x49,0x49,0x49,0x36}, // 0x38 8
    {0x06,0x49,0x49,0x29,0x1E}, // 0x39 9
    {0x00,0x36,0x36,0x00,0x00}, // 0x3A :
    {0x00,0x56,0x36,0x00,0x00}, // 0x3B ;
    {0x08,0x14,0x22,0x41,0x00}, // 0x3C <
    {0x14,0x14,0x14,0x14,0x14}, // 0x3D =
    {0x00,0x41,0x22,0x14,0x08}, // 0x3E >
    {0x02,0x01,0x51,0x09,0x06}, // 0x3F ?
    {0x32,0x49,0x79,0x41,0x3E}, // 0x40 @
    {0x7E,0x11,0x11,0x11,0x7E}, // 0x41 A
    {0x7F,0x49,0x49,0x49,0x36}, // 0x42 B
    {0x3E,0x41,0x41,0x41,0x22}, // 0x43 C
    {0x7F,0x41,0x41,0x22,0x1C}, // 0x44 D
    {0x7F,0x49,0x49,0x49,0x41}, // 0x45 E
    {0x7F,0x09,0x09,0x09,0x01}, // 0x46 F
    {0x3E,0x41,0x49,0x49,0x7A}, // 0x47 G
    {0x7F,0x08,0x08,0x08,0x7F}, // 0x48 H
    {0x00,0x41,0x7F,0x41,0x00}, // 0x49 I
    {0x20,0x40,0x41,0x3F,0x01}, // 0x4A J
    {0x7F,0x08,0x14,0x22,0x41}, // 0x4B K
    {0x7F,0x40,0x40,0x40,0x40}, // 0x4C L
    {0x7F,0x02,0x0C,0x02,0x7F}, // 0x4D M
    {0x7F,0x04,0x08,0x10,0x7F}, // 0x4E N
    {0x3E,0x41,0x41,0x41,0x3E}, // 0x4F O
    {0x7F,0x09,0x09,0x09,0x06}, // 0x50 P
    {0x3E,0x41,0x51,0x21,0x5E}, // 0x51 Q
    {0x7F,0x09,0x19,0x29,0x46}, // 0x52 R
    {0x46,0x49,0x49,0x49,0x31}, // 0x53 S
    {0x01,0x01,0x7F,0x01,0x01}, // 0x54 T
    {0x3F,0x40,0x40,0x40,0x3F}, // 0x55 U
    {0x1F,0x20,0x40,0x20,0x1F}, // 0x56 V
    {0x3F,0x40,0x38,0x40,0x3F}, // 0x57 W
    {0x63,0x14,0x08,0x14,0x63}, // 0x58 X
    {0x07,0x08,0x70,0x08,0x07}, // 0x59 Y
    {0x61,0x51,0x49,0x45,0x43}, // 0x5A Z
    {0x00,0x7F,0x41,0x41,0x00}, // 0x5B [
    {0x02,0x04,0x08,0x10,0x20}, // 0x5C backslash
    {0x00,0x41,0x41,0x7F,0x00}, // 0x5D ]
    {0x04,0x02,0x01,0x02,0x04}, // 0x5E ^
    {0x40,0x40,0x40,0x40,0x40}, // 0x5F _
    {0x00,0x01,0x02,0x04,0x00}, // 0x60 `
    {0x20,0x54,0x54,0x54,0x78}, // 0x61 a
    {0x7F,0x48,0x44,0x44,0x38}, // 0x62 b
    {0x38,0x44,0x44,0x44,0x20}, // 0x63 c
    {0x38,0x44,0x44,0x48,0x7F}, // 0x64 d
    {0x38,0x54,0x54,0x54,0x18}, // 0x65 e
    {0x08,0x7E,0x09,0x01,0x02}, // 0x66 f
    {0x0C,0x52,0x52,0x52,0x3E}, // 0x67 g
    {0x7F,0x08,0x04,0x04,0x78}, // 0x68 h
    {0x00,0x44,0x7D,0x40,0x00}, // 0x69 i
    {0x20,0x40,0x44,0x3D,0x00}, // 0x6A j
    {0x7F,0x10,0x28,0x44,0x00}, // 0x6B k
    {0x00,0x41,0x7F,0x40,0x00}, // 0x6C l
    {0x7C,0x04,0x18,0x04,0x78}, // 0x6D m
    {0x7C,0x08,0x04,0x04,0x78}, // 0x6E n
    {0x38,0x44,0x44,0x44,0x38}, // 0x6F o
    {0x7C,0x14,0x14,0x14,0x08}, // 0x70 p
    {0x08,0x14,0x14,0x14,0x7C}, // 0x71 q
    {0x7C,0x08,0x04,0x04,0x08}, // 0x72 r
    {0x48,0x54,0x54,0x54,0x20}, // 0x73 s
    {0x04,0x3F,0x44,0x40,0x20}, // 0x74 t
    {0x3C,0x40,0x40,0x20,0x7C}, // 0x75 u
    {0x1C,0x20,0x40,0x20,0x1C}, // 0x76 v
    {0x3C,0x40,0x30,0x40,0x3C}, // 0x77 w
    {0x44,0x28,0x10,0x28,0x44}, // 0x78 x
    {0x0C,0x50,0x50,0x50,0x3C}, // 0x79 y
    {0x44,0x64,0x54,0x4C,0x44}, // 0x7A z
    {0x00,0x08,0x36,0x41,0x00}, // 0x7B {
    {0x00,0x00,0x7F,0x00,0x00}, // 0x7C |
    {0x00,0x41,0x36,0x08,0x00}, // 0x7D }
    {0x10,0x08,0x08,0x10,0x08}, // 0x7E ~
};

/* Draw one character into the page-format framebuffer */
static void draw_char(uint8_t col, uint8_t page, char c)
{
    if (c < 0x20 || c > 0x7E) c = 0x20;
    const uint8_t *g = FONT5X7[(uint8_t)c - 0x20];

    for (int i = 0; i < 5; i++) {
        if (col + i >= OLED_WIDTH) break;
        s_framebuffer[page][col + i] = g[i];
    }
    /* 1 pixel gap */
    if (col + 5 < OLED_WIDTH) s_framebuffer[page][col + 5] = 0x00;
}

void oled_ui_write(uint8_t col, uint8_t page, const char *str)
{
    if (!s_initialized || !str) return;
    if (page >= OLED_PAGES) return;

    uint8_t x = col;
    while (*str && x < OLED_WIDTH) {
        draw_char(x, page, *str++);
        x += 6;
    }
}

void oled_ui_set_device_id(const char *id)
{
    if (!id) return;
    strncpy(s_device_id, id, sizeof(s_device_id) - 1);
    s_device_id[sizeof(s_device_id) - 1] = '\0';
}

void oled_ui_set_state(oled_state_t state)
{
    if (!s_initialized) return;
    oled_ui_clear();

    switch (state) {
        case OLED_STATE_INIT:
            oled_ui_write(0, 0, "ESPClaw v0.2.0");
            oled_ui_write(0, 1, "Initializing...");
            break;
        case OLED_STATE_WIFI_CONFIG:
            oled_ui_write(0, 0, "== WiFi Config ==");
            oled_ui_write(0, 2, "AP: ESPClaw-XXXXX");
            oled_ui_write(0, 3, "PW: espclaw1");
            oled_ui_write(0, 4, "-> 192.168.4.1");
            break;
        case OLED_STATE_CONNECTING:
            oled_ui_write(0, 0, "ESPClaw v0.2.0");
            oled_ui_write(0, 2, "WiFi connecting...");
            break;
        case OLED_STATE_CONNECTED:
            oled_ui_write(0, 0, "ESPClaw v0.2.0");
            oled_ui_write(0, 2, "WiFi Connected!");
            break;
        case OLED_STATE_LLM_CONFIG:
            oled_ui_write(0, 0, "ESPClaw v0.2.0");
            oled_ui_write(0, 2, "Set LLM key");
            oled_ui_write(0, 3, "via web UI");
            break;
        case OLED_STATE_READY: {
            oled_ui_write(0, 0, "ESPClaw v0.2.0");
            oled_ui_write(0, 2, "** READY **");
            char ip_tmp[16];
            if (wifi_mgr_get_ip_str(ip_tmp, sizeof(ip_tmp)) && strlen(ip_tmp) > 0) {
                oled_ui_write(0, 4, ip_tmp);
            }
            break;
        }
        case OLED_STATE_LISTENING:
            oled_ui_write(0, 0, "ESPClaw v0.2.0");
            oled_ui_write(0, 2, "** LISTEN **");
            oled_ui_write(0, 3, "Wake word ON");
            break;
        case OLED_STATE_WAKE:
            oled_ui_write(0, 0, "ESPClaw v0.2.0");
            oled_ui_write(0, 2, "** WAKE! **");
            oled_ui_write(0, 3, "Agent...");
            break;
        case OLED_STATE_THINKING:
            oled_ui_write(0, 0, "ESPClaw v0.2.0");
            oled_ui_write(0, 2, "** THINKING **");
            oled_ui_write(0, 3, "Processing...");
            break;
        case OLED_STATE_RECORDING:
            oled_ui_write(0, 0, "ESPClaw v0.2.0");
            oled_ui_write(0, 2, "** RECORDING *");
            oled_ui_write(0, 3, "Dang thu am...");
            break;
        case OLED_STATE_ERROR:
            oled_ui_write(0, 0, "== ERROR ==");
            oled_ui_write(0, 2, "Check serial log");
            break;
        default: break;
    }
    /* Always show Device ID at the bottom */
    draw_device_id();
    oled_ui_update();
}

void oled_ui_update_ip(const char *ip)
{
    if (!s_initialized || !ip) return;
    oled_ui_write(0, 4, ip);
    oled_ui_update();
}

void oled_ui_update_wifi(const char *ssid) { (void)ssid; }

void oled_ui_show_message(const char *line1, const char *line2, const char *line3)
{
    if (!s_initialized) return;
    oled_ui_clear();
    if (line1) oled_ui_write(0, 0, line1);
    if (line2) oled_ui_write(0, 2, line2);
    if (line3) oled_ui_write(0, 4, line3);
    oled_ui_update();
}
