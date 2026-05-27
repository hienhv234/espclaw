/*
 * ESPClaw - neuron_cache.h
 * NVS-based L1 cache for Neuron Graph data
 * Stores nodes, links, and pulses locally on device using NVS
 */
#ifndef NEURON_CACHE_H
#define NEURON_CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cache entry types */
typedef enum {
    CACHE_NODE = 0,
    CACHE_LINK = 1,
    CACHE_PULSE = 2,
} cache_entry_type_t;

/* Node cache entry */
typedef struct {
    char id[37];           /* UUID string */
    char tenant_id[37];    /* Tenant UUID */
    char type[32];         /* node_type enum */
    char name[128];
    char subtype[64];
    double pos_x, pos_y, pos_z;
    char *embedding;        /* Base64 encoded vector */
    cJSON *content;        /* JSON metadata */
    bool is_active;
    uint64_t updated_at;   /* Unix timestamp */
    bool synced;           /* Local-only if true */
} cache_node_t;

/* Link cache entry */
typedef struct {
    char id[37];
    char tenant_id[37];
    char source_id[37];
    char target_id[37];
    char type[32];
    double weight;
    cJSON *metadata;
    uint64_t updated_at;
    bool synced;
} cache_link_t;

/* Pulse cache entry */
typedef struct {
    char id[37];
    char tenant_id[37];
    char device_id[37];
    char type[32];
    char source_id[37];
    char target_id[37];
    char *text;
    double energy;
    uint64_t created_at;
    bool synced;
} cache_pulse_t;

/* Cache statistics */
typedef struct {
    uint32_t node_count;
    uint32_t link_count;
    uint32_t pulse_count;
    uint32_t pending_sync;
    uint64_t last_sync_at;
} cache_stats_t;

/* ============================================================
 * Initialization
 * ============================================================ */

/**
 * Initialize the NVS cache
 * @return ESP_OK on success
 */
esp_err_t neuron_cache_init(void);

/**
 * Deinitialize and close the cache
 */
esp_err_t neuron_cache_deinit(void);

/**
 * Get cache statistics
 */
esp_err_t neuron_cache_get_stats(cache_stats_t *stats);

/* ============================================================
 * Node Operations
 * ============================================================ */

/**
 * Insert or update a node in cache
 */
esp_err_t neuron_cache_upsert_node(const cache_node_t *node);

/**
 * Get a node by ID
 */
esp_err_t neuron_cache_get_node(const char *id, cache_node_t **out_node);

/**
 * Delete a node from cache
 */
esp_err_t neuron_cache_delete_node(const char *id);

/**
 * Get all nodes for a tenant
 */
cache_node_t** neuron_cache_get_nodes_by_tenant(const char *tenant_id, uint32_t *out_count);

/**
 * Get unsynced nodes
 */
cache_node_t** neuron_cache_get_unsynced_nodes(uint32_t *out_count);

/**
 * Mark node as synced
 */
esp_err_t neuron_cache_mark_node_synced(const char *id);

/* ============================================================
 * Link Operations
 * ============================================================ */

/**
 * Insert or update a link in cache
 */
esp_err_t neuron_cache_upsert_link(const cache_link_t *link);

/**
 * Get a link by ID
 */
esp_err_t neuron_cache_get_link(const char *id, cache_link_t **out_link);

/**
 * Delete a link from cache
 */
esp_err_t neuron_cache_delete_link(const char *id);

/**
 * Get links by tenant
 */
cache_link_t** neuron_cache_get_links_by_tenant(const char *tenant_id, uint32_t *out_count);

/**
 * Get links for a specific source node
 */
cache_link_t** neuron_cache_get_links_by_source(const char *source_id, uint32_t *out_count);

/**
 * Get unsynced links
 */
cache_link_t** neuron_cache_get_unsynced_links(uint32_t *out_count);

/**
 * Mark link as synced
 */
esp_err_t neuron_cache_mark_link_synced(const char *id);

/* ============================================================
 * Pulse Operations
 * ============================================================ */

/**
 * Insert a pulse (always create new, never update)
 */
esp_err_t neuron_cache_insert_pulse(const cache_pulse_t *pulse);

/**
 * Get pulses by tenant
 */
cache_pulse_t** neuron_cache_get_pulses_by_tenant(const char *tenant_id, uint32_t *out_count);

/**
 * Get pulses by node (as source or target)
 */
cache_pulse_t** neuron_cache_get_pulses_by_node(const char *node_id, uint32_t *out_count);

/**
 * Get unsynced pulses
 */
cache_pulse_t** neuron_cache_get_unsynced_pulses(uint32_t *out_count);

/**
 * Mark pulse as synced
 */
esp_err_t neuron_cache_mark_pulse_synced(const char *id);

/* ============================================================
 * Sync Operations
 * ============================================================ */

/**
 * Set last sync timestamp
 */
esp_err_t neuron_cache_set_last_sync(uint64_t timestamp);

/**
 * Get last sync timestamp
 */
uint64_t neuron_cache_get_last_sync(void);

/**
 * Clear all cached data
 */
esp_err_t neuron_cache_reset(void);

/* ============================================================
 * Memory Management
 * ============================================================ */

/**
 * Free a cache node
 */
void neuron_cache_free_node(cache_node_t *node);

/**
 * Free a cache link
 */
void neuron_cache_free_link(cache_link_t *link);

/**
 * Free a cache pulse
 */
void neuron_cache_free_pulse(cache_pulse_t *pulse);

/**
 * Free an array of nodes
 */
void neuron_cache_free_nodes(cache_node_t **nodes, uint32_t count);

/**
 * Free an array of links
 */
void neuron_cache_free_links(cache_link_t **links, uint32_t count);

/**
 * Free an array of pulses
 */
void neuron_cache_free_pulses(cache_pulse_t **pulses, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* NEURON_CACHE_H */
