/*
 * ESPClaw - net/wifi_manager.h
 * WiFi STA connection manager.
 */
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*wifi_ip_callback_t)(const char *ip_str, void *user_data);

/* Initialize WiFi stack once */
esp_err_t wifi_mgr_init_common(void);

/* Initialize WiFi STA and connect using NVS or Kconfig credentials */
esp_err_t wifi_mgr_init_and_connect(void);

/* Check if WiFi is connected */
bool wifi_mgr_is_connected(void);

/* Get current IP address as string */
bool wifi_mgr_get_ip_str(char *buf, size_t len);

/* Register callback for when IP is obtained */
void wifi_mgr_set_ip_callback(wifi_ip_callback_t callback, void *user_data);

#endif /* WIFI_MANAGER_H */
