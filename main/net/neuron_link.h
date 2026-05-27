/*
 * ESPClaw - neuron_link.h
 * Neuron Link Protocol - Sync ESP32 cache with Supabase
 */
#ifndef NEURON_LINK_H
#define NEURON_LINK_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sync modes */
typedef enum {
    SYNC_MODE_DELTA = 0,   /* Only changed since last sync */
    SYNC_MODE_FULL = 1,    /* Full graph sync */
    SYNC_MODE_FORCE = 2,  /* Force full sync, ignore last_sync */
} sync_mode_t;

/* Sync direction */
typedef enum {
    SYNC_DIR_DEVICE_TO_CLOUD = 0,
    SYNC_DIR_CLOUD_TO_DEVICE = 1,
    SYNC_DIR_BIDIRECTIONAL = 2,
} sync_direction_t;

/* Sync status */
typedef enum {
    NL_STATUS_IDLE = 0,
    NL_STATUS_CONNECTING = 1,
    NL_STATUS_SYNCING = 2,
    NL_STATUS_COMPLETE = 3,
    NL_STATUS_ERROR = 4,
} nl_status_t;

/* Sync result */
typedef struct {
    nl_status_t status;
    uint32_t nodes_sent;
    uint32_t nodes_received;
    uint32_t links_sent;
    uint32_t links_received;
    uint32_t pulses_sent;
    uint32_t pulses_received;
    uint64_t duration_ms;
    char error_msg[256];
} nl_sync_result_t;

/* Sync configuration */
typedef struct {
    char *supabase_url;
    char *supabase_key;
    char *device_id;
    char *tenant_id;
    sync_mode_t mode;
    sync_direction_t direction;
    uint32_t batch_size;      /* Items per batch */
    uint32_t timeout_ms;      /* Request timeout */
    bool auto_sync;           /* Enable auto-sync */
    uint32_t auto_sync_interval_sec;
} nl_config_t;

/* ============================================================
 * Initialization
 * ============================================================ */

/**
 * Initialize Neuron Link
 * @param config Configuration (copied internally)
 * @return ESP_OK on success
 */
esp_err_t neuron_link_init(const nl_config_t *config);

/**
 * Deinitialize Neuron Link
 */
esp_err_t neuron_link_deinit(void);

/**
 * Update configuration at runtime
 */
esp_err_t neuron_link_set_config(const nl_config_t *config);

/**
 * Get current configuration
 */
const nl_config_t* neuron_link_get_config(void);

/* ============================================================
 * Connection
 * ============================================================ */

/**
 * Check if connected to Supabase
 */
bool neuron_link_is_connected(void);

/**
 * Force reconnect
 */
esp_err_t neuron_link_connect(void);

/**
 * Disconnect
 */
esp_err_t neuron_link_disconnect(void);

/* ============================================================
 * Sync Operations
 * ============================================================ */

/**
 * Perform sync operation
 * @param mode Delta or Full sync
 * @param direction Sync direction
 * @param result Pointer to receive result
 * @return ESP_OK on success
 */
esp_err_t neuron_link_sync(sync_mode_t mode, sync_direction_t direction, nl_sync_result_t *result);

/**
 * Quick delta sync (device to cloud only)
 */
esp_err_t neuron_link_delta_sync(void);

/**
 * Full sync (bidirectional)
 */
esp_err_t neuron_link_full_sync(void);

/**
 * Get last sync result
 */
const nl_sync_result_t* neuron_link_get_last_result(void);

/**
 * Get last sync timestamp
 */
uint64_t neuron_link_get_last_sync_time(void);

/* ============================================================
 * Node Operations (via Neuron Link)
 * ============================================================ */

/**
 * Push node to cloud
 */
esp_err_t neuron_link_push_node(const char *node_json);

/**
 * Pull nodes from cloud
 * @param since Unix timestamp (0 for all)
 * @return JSON array of nodes
 */
char* neuron_link_pull_nodes(uint64_t since);

/**
 * Delete node from cloud
 */
esp_err_t neuron_link_delete_node(const char *node_id);

/* ============================================================
 * Link Operations (via Neuron Link)
 * ============================================================ */

/**
 * Push link to cloud
 */
esp_err_t neuron_link_push_link(const char *link_json);

/**
 * Pull links from cloud
 */
char* neuron_link_pull_links(uint64_t since);

/**
 * Delete link from cloud
 */
esp_err_t neuron_link_delete_link(const char *link_id);

/* ============================================================
 * Pulse Operations (via Neuron Link)
 * ============================================================ */

/**
 * Push pulse to cloud (fire and forget)
 */
esp_err_t neuron_link_push_pulse(const char *pulse_json);

/**
 * Pull pulses from cloud (for playback/analysis)
 */
char* neuron_link_pull_pulses(const char *node_id, uint32_t limit);

/* ============================================================
 * Real-time Updates (WebSocket)
 * ============================================================ */

/**
 * Subscribe to real-time node updates
 */
esp_err_t neuron_link_subscribe_nodes(void (*callback)(const char *payload));

/**
 * Subscribe to real-time link updates
 */
esp_err_t neuron_link_subscribe_links(void (*callback)(const char *payload));

/**
 * Subscribe to real-time pulse events
 */
esp_err_t neuron_link_subscribe_pulses(void (*callback)(const char *payload));

/**
 * Unsubscribe from real-time updates
 */
esp_err_t neuron_link_unsubscribe_all(void);

/* ============================================================
 * Workflow Sync (Cloud → Device)
 * ============================================================ */

/**
 * Pull workflows from cloud and load into workflow engine.
 * Called during sync cycle.
 * @return ESP_OK on success
 */
esp_err_t neuron_link_sync_workflows(void);

/**
 * Pull workflows for a specific tenant (called from agent).
 */
esp_err_t neuron_link_pull_workflows(void);

/**
 * Selective config sync from cloud to device.
 * Only applies cloud config if local NVS key is empty.
 * Called on explicit user request from web UI.
 * @return ESP_OK on success
 */
esp_err_t neuron_link_sync_config_only(void);

/* ============================================================
 * Status & Monitoring
 * ============================================================ */

/**
 * Get current status
 */
nl_status_t neuron_link_get_status(void);

/**
 * Get connection info
 */
void neuron_link_get_connection_info(char *buf, size_t buf_len);

/* ============================================================
 * Device Pairing
 * ============================================================ */

/**
 * Generate pairing code
 * @param out_code 6-digit code (caller allocates 7 bytes)
 */
esp_err_t neuron_link_generate_pairing_code(char *out_code);

/**
 * Check pairing status
 * @return true if device is paired
 */
bool neuron_link_is_paired(void);

/**
 * Get paired tenant info
 */
esp_err_t neuron_link_get_tenant_info(char *tenant_id, char *tenant_name);

/**
 * Unpair device
 */
esp_err_t neuron_link_unpair(void);

#ifdef __cplusplus
}
#endif

#endif /* NEURON_LINK_H */
