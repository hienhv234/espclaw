/*
 * ESPClaw - mem/nvs_manager.h
 * NVS read/write abstraction layer.
 */
#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Initialize NVS flash (handles corrupt partition recovery) */
esp_err_t nvs_mgr_init(void);

/* String operations (default namespace) */
bool nvs_mgr_get_str(const char *key, char *value, size_t max_len);
esp_err_t nvs_mgr_set_str(const char *key, const char *value);

/* Integer operations */
bool nvs_mgr_get_i32(const char *key, int32_t *value);
esp_err_t nvs_mgr_set_i32(const char *key, int32_t value);

/* Additional NVS operations with custom namespace */
esp_err_t nvs_mgr_set_str_ns(const char *ns, const char *key, const char *value);
esp_err_t nvs_mgr_erase_key(const char *ns, const char *key);

#endif /* NVS_MANAGER_H */
