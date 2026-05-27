/*
 * ESPClaw - tool/tool_led.c
 * WS2812 RGB LED / NeoPixel control and asynchronous effects tool.
 */
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "led_strip_types.h"
#include "util/json_util.h"
#include "hal/hal_gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG_LED = "tool_led";

static led_strip_handle_t s_strip_handle = NULL;
static int s_cached_pin = -1;
static int s_cached_count = -1;

static TaskHandle_t s_effect_task_handle = NULL;
static volatile bool s_stop_effect = false;

typedef struct {
    int pin;
    char effect[16]; // "blink", "cycle", "rainbow"
    int r, g, b;
    int delay_ms;
    int count;
} led_effect_config_t;

static led_effect_config_t s_effect_config;

/* Expose to internal C modules */
void tool_led_set_status(const char *effect, int r, int g, int b, int delay_ms);

static void stop_current_effect(void)
{
    if (s_effect_task_handle) {
        s_stop_effect = true;
        int timeout = 50;
        while (s_effect_task_handle && timeout-- > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_effect_task_handle) {
            vTaskDelete(s_effect_task_handle);
            s_effect_task_handle = NULL;
        }
    }
    s_stop_effect = false;
}

static void led_effect_task(void *pvParameters)
{
    led_effect_config_t *cfg = &s_effect_config;
    int pin = cfg->pin;
    int delay_ms = cfg->delay_ms <= 0 ? 500 : cfg->delay_ms;
    int count = cfg->count <= 0 ? 10 : cfg->count;

    ESP_LOGI(TAG_LED, "Starting effect '%s' on pin %d", cfg->effect, pin);

    if (strcmp(cfg->effect, "blink") == 0) {
        for (int i = 0; i < count && !s_stop_effect; i++) {
            // Turn ON
            led_strip_set_pixel(s_strip_handle, 0, cfg->r, cfg->g, cfg->b);
            led_strip_refresh(s_strip_handle);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));

            if (s_stop_effect) break;

            // Turn OFF
            led_strip_set_pixel(s_strip_handle, 0, 0, 0, 0);
            led_strip_refresh(s_strip_handle);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    } else if (strcmp(cfg->effect, "cycle") == 0) {
        // Red, Green, Blue, Yellow, Purple, Cyan, White
        int colors[][3] = {
            {255, 0, 0},     // Red
            {0, 255, 0},     // Green
            {0, 0, 255},     // Blue
            {255, 255, 0},   // Yellow
            {255, 0, 255},   // Purple
            {0, 255, 255},   // Cyan
            {255, 255, 255}  // White
        };
        int num_colors = 7;
        for (int i = 0; i < count && !s_stop_effect; i++) {
            int idx = i % num_colors;
            led_strip_set_pixel(s_strip_handle, 0, colors[idx][0], colors[idx][1], colors[idx][2]);
            led_strip_refresh(s_strip_handle);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    } else if (strcmp(cfg->effect, "rainbow") == 0) {
        // Rainbow color spectrum transition
        int hue = 0;
        // count represents total seconds for the rainbow
        int steps = count * 1000 / 30;
        for (int i = 0; i < steps && !s_stop_effect; i++) {
            int h = (hue / 60) % 6;
            int f = hue % 60;
            int v = 255;
            int p = 0;
            int q = v * (60 - f) / 60;
            int t = v * f / 60;
            uint32_t r = 0, g = 0, b = 0;
            switch(h) {
                case 0: r=v; g=t; b=p; break;
                case 1: r=q; g=v; b=p; break;
                case 2: r=p; g=v; b=t; break;
                case 3: r=p; g=q; b=v; break;
                case 4: r=t; g=p; b=v; break;
                default: r=v; g=p; b=q; break;
            }
            led_strip_set_pixel(s_strip_handle, 0, r, g, b);
            led_strip_refresh(s_strip_handle);
            hue = (hue + 2) % 360;
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }

    if (s_strip_handle) {
        led_strip_clear(s_strip_handle);
    }
    
    s_effect_task_handle = NULL;
    ESP_LOGI(TAG_LED, "Effect finished and cleared");
    vTaskDelete(NULL);
}

static bool tool_rgb_led_set(const char *input_json, char *result_buf, size_t result_sz)
{
    stop_current_effect();

    int pin = 48; /* Default pin is 48 (Onboard RGB LED) */
    int r = -1, g = -1, b = -1;
    int index = 0, count = 1;

    json_get_int(input_json, "pin", &pin);
    if (!json_get_int(input_json, "r",   &r)   ||
        !json_get_int(input_json, "g",   &g)   ||
        !json_get_int(input_json, "b",   &b)) {
        snprintf(result_buf, result_sz, "Error: missing required color channels r, g, or b");
        return false;
    }

    // Read optional args
    json_get_int(input_json, "index", &index);
    json_get_int(input_json, "count", &count);

    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        snprintf(result_buf, result_sz, "Error: RGB colors must be between 0 and 255");
        return false;
    }

    if (!hal_gpio_is_allowed(pin)) {
        snprintf(result_buf, result_sz, "Error: GPIO pin %d is not allowed/safe", pin);
        return false;
    }

    // If cached handle exists but pin or count changed, delete old handle first to release RMT channel
    if (s_strip_handle && (s_cached_pin != pin || s_cached_count != count)) {
        led_strip_del(s_strip_handle);
        s_strip_handle = NULL;
        s_cached_pin = -1;
        s_cached_count = -1;
    }

    // Initialize dynamic led_strip handle
    if (!s_strip_handle) {
        led_strip_config_t strip_config = {
            .strip_gpio_num = pin,
            .max_leds = (uint32_t)count,
            .led_model = LED_MODEL_WS2812,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
            .flags = {
                .invert_out = false,
            }
        };
        led_strip_rmt_config_t rmt_config = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000, /* 10MHz */
            .mem_block_symbols = 0,
            .flags = {
                .with_dma = 0,
            }
        };
        esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip_handle);
        if (err != ESP_OK) {
            snprintf(result_buf, result_sz, "Error: Failed to init LED strip on pin %d: %s", pin, esp_err_to_name(err));
            s_strip_handle = NULL;
            return false;
        }
        s_cached_pin = pin;
        s_cached_count = count;
    }

    if (index < 0 || index >= count) {
        snprintf(result_buf, result_sz, "Error: Index %d out of bounds (0-%d)", index, count - 1);
        return false;
    }

    esp_err_t err_set = led_strip_set_pixel(s_strip_handle, (uint32_t)index, (uint32_t)r, (uint32_t)g, (uint32_t)b);
    if (err_set != ESP_OK) {
        snprintf(result_buf, result_sz, "Error: Set pixel failed: %s", esp_err_to_name(err_set));
        return false;
    }

    esp_err_t err_ref = led_strip_refresh(s_strip_handle);
    if (err_ref != ESP_OK) {
        snprintf(result_buf, result_sz, "Error: Refresh failed: %s", esp_err_to_name(err_ref));
        return false;
    }

    snprintf(result_buf, result_sz, "Success: Pin %d [index %d] set to RGB(%d,%d,%d)", pin, index, r, g, b);
    return true;
}

static bool tool_rgb_led_effect(const char *input_json, char *result_buf, size_t result_sz)
{
    stop_current_effect();

    int pin = 48;
    char effect[16] = "blink";
    int r = 255, g = 0, b = 0;
    int delay_ms = 500;
    int count = 10;

    json_get_int(input_json, "pin", &pin);
    json_get_str(input_json, "effect", effect, sizeof(effect));
    json_get_int(input_json, "r", &r);
    json_get_int(input_json, "g", &g);
    json_get_int(input_json, "b", &b);
    json_get_int(input_json, "delay_ms", &delay_ms);
    json_get_int(input_json, "count", &count);

    if (!hal_gpio_is_allowed(pin)) {
        snprintf(result_buf, result_sz, "Error: GPIO pin %d is not allowed/safe", pin);
        return false;
    }

    if (s_strip_handle && s_cached_pin != pin) {
        led_strip_del(s_strip_handle);
        s_strip_handle = NULL;
        s_cached_pin = -1;
        s_cached_count = -1;
    }

    if (!s_strip_handle) {
        led_strip_config_t strip_config = {
            .strip_gpio_num = pin,
            .max_leds = 1,
            .led_model = LED_MODEL_WS2812,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
            .flags = {
                .invert_out = false,
            }
        };
        led_strip_rmt_config_t rmt_config = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000,
            .mem_block_symbols = 0,
            .flags = {
                .with_dma = 0,
            }
        };
        esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip_handle);
        if (err != ESP_OK) {
            snprintf(result_buf, result_sz, "Error: Failed to init LED strip: %s", esp_err_to_name(err));
            s_strip_handle = NULL;
            return false;
        }
        s_cached_pin = pin;
        s_cached_count = 1;
    }

    s_effect_config.pin = pin;
    strncpy(s_effect_config.effect, effect, sizeof(s_effect_config.effect) - 1);
    s_effect_config.r = r;
    s_effect_config.g = g;
    s_effect_config.b = b;
    s_effect_config.delay_ms = delay_ms;
    s_effect_config.count = count;

    BaseType_t ret = xTaskCreatePinnedToCore(
        led_effect_task,
        "led_effect_task",
        4096,
        NULL,
        5,
        &s_effect_task_handle,
        PRO_CPU_NUM
    );

    if (ret != pdPASS) {
        snprintf(result_buf, result_sz, "Error: Failed to create background LED effect task");
        return false;
    }

    snprintf(result_buf, result_sz, "Success: Started background LED effect '%s' on pin %d", effect, pin);
    return true;
}

void tool_led_set_status(const char *effect, int r, int g, int b, int delay_ms)
{
    char input_json[128];
    char result_buf[128];
    if (strcmp(effect, "solid") == 0 || strcmp(effect, "off") == 0) {
        snprintf(input_json, sizeof(input_json), "{\"pin\":48,\"r\":%d,\"g\":%d,\"b\":%d}", r, g, b);
        tool_rgb_led_set(input_json, result_buf, sizeof(result_buf));
    } else {
        snprintf(input_json, sizeof(input_json), "{\"pin\":48,\"effect\":\"%s\",\"r\":%d,\"g\":%d,\"b\":%d,\"delay_ms\":%d}",
                 effect, r, g, b, delay_ms);
        tool_rgb_led_effect(input_json, result_buf, sizeof(result_buf));
    }
}
