/*
 * ESPClaw - neuron_cache.c
 * NVS-based L1 cache for Neuron Graph data
 * Uses JSON serialization in NVS for simplicity on ESP32
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "neuron_cache.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mem/nvs_manager.h"
#include "cJSON.h"

static const char *TAG = "neuron_cache";

#define NVS_NAMESPACE "ncache"

/* JSON keys */
#define KEY_NODES "nodes"
#define KEY_LINKS "links"
#define KEY_PULSES "pulses"
#define KEY_LAST_SYNC "last_sync"
#define KEY_COUNTS "counts"

/* Max items in cache (memory constraint) */
#define MAX_NODES 100
#define MAX_LINKS 200
#define MAX_PULSES 500

/* Static storage */
static bool s_initialized = false;
static nvs_handle_t s_nvs_handle = 0;
static cJSON *s_nodes = NULL;
static cJSON *s_links = NULL;
static cJSON *s_pulses = NULL;
static uint64_t s_last_sync = 0;

/* ============================================================
 * Helper Functions
 * ============================================================ */

static inline cJSON* get_or_create_object(cJSON *parent, const char *key) {
    cJSON *obj = cJSON_GetObjectItem(parent, key);
    if (!obj) {
        obj = cJSON_CreateObject();
        cJSON_AddItemToObject(parent, key, obj);
    }
    return obj;
}

static cJSON* load_json_from_nvs(const char *key) {
    size_t len = 0;
    esp_err_t err = nvs_get_blob(s_nvs_handle, key, NULL, &len);
    
    if (err == ESP_OK && len > 0) {
        char *buf = malloc(len + 1);
        if (buf) {
            if (nvs_get_blob(s_nvs_handle, key, buf, &len) == ESP_OK) {
                buf[len] = '\0';
                cJSON *json = cJSON_Parse(buf);
                free(buf);
                if (json) return json;
            } else {
                free(buf);
            }
        }
    }
    return cJSON_CreateArray();
}

static esp_err_t save_json_to_nvs(const char *key, cJSON *json) {
    char *str = cJSON_PrintUnformatted(json);
    if (!str) return ESP_ERR_NO_MEM;
    
    esp_err_t err = nvs_set_blob(s_nvs_handle, key, str, strlen(str));
    free(str);
    
    if (err == ESP_OK) {
        nvs_commit(s_nvs_handle);
    }
    
    return err;
}

/* ============================================================
 * Initialization
 * ============================================================ */

esp_err_t neuron_cache_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing neuron cache...");
    
    /* Open NVS namespace */
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Load data from NVS */
    s_nodes = load_json_from_nvs(KEY_NODES);
    s_links = load_json_from_nvs(KEY_LINKS);
    s_pulses = load_json_from_nvs(KEY_PULSES);
    
    if (!s_nodes) s_nodes = cJSON_CreateArray();
    if (!s_links) s_links = cJSON_CreateArray();
    if (!s_pulses) s_pulses = cJSON_CreateArray();
    
    /* Load last sync time */
    nvs_get_u64(s_nvs_handle, KEY_LAST_SYNC, &s_last_sync);
    
    s_initialized = true;
    
    ESP_LOGI(TAG, "Neuron cache initialized: %d nodes, %d links, %d pulses",
             cJSON_GetArraySize(s_nodes),
             cJSON_GetArraySize(s_links),
             cJSON_GetArraySize(s_pulses));
    
    return ESP_OK;
}

esp_err_t neuron_cache_deinit(void) {
    if (!s_initialized) return ESP_OK;
    
    /* Save to NVS before deinit */
    save_json_to_nvs(KEY_NODES, s_nodes);
    save_json_to_nvs(KEY_LINKS, s_links);
    save_json_to_nvs(KEY_PULSES, s_pulses);
    
    if (s_nodes) cJSON_Delete(s_nodes);
    if (s_links) cJSON_Delete(s_links);
    if (s_pulses) cJSON_Delete(s_pulses);
    
    s_nodes = s_links = s_pulses = NULL;
    s_initialized = false;
    
    return ESP_OK;
}

esp_err_t neuron_cache_get_stats(cache_stats_t *stats) {
    if (!s_initialized || !stats) return ESP_ERR_INVALID_ARG;
    
    memset(stats, 0, sizeof(cache_stats_t));
    
    stats->node_count = cJSON_GetArraySize(s_nodes);
    stats->link_count = cJSON_GetArraySize(s_links);
    stats->pulse_count = cJSON_GetArraySize(s_pulses);
    stats->last_sync_at = s_last_sync;
    
    /* Count unsynced */
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_nodes) {
        if (!cJSON_GetObjectItem(item, "synced") || 
            !cJSON_IsTrue(cJSON_GetObjectItem(item, "synced"))) {
            stats->pending_sync++;
        }
    }
    
    cJSON_ArrayForEach(item, s_links) {
        if (!cJSON_GetObjectItem(item, "synced") || 
            !cJSON_IsTrue(cJSON_GetObjectItem(item, "synced"))) {
            stats->pending_sync++;
        }
    }
    
    cJSON_ArrayForEach(item, s_pulses) {
        if (!cJSON_GetObjectItem(item, "synced") || 
            !cJSON_IsTrue(cJSON_GetObjectItem(item, "synced"))) {
            stats->pending_sync++;
        }
    }
    
    return ESP_OK;
}

/* ============================================================
 * Node Operations
 * ============================================================ */

esp_err_t neuron_cache_upsert_node(const cache_node_t *node) {
    if (!s_initialized || !node) return ESP_ERR_INVALID_ARG;
    
    /* Find existing */
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_nodes) {
        cJSON *id = cJSON_GetObjectItem(item, "id");
        if (id && strcmp(id->valuestring, node->id) == 0) {
            /* Update existing */
            cJSON_DeleteItemFromObject(item, "type");
            cJSON_DeleteItemFromObject(item, "name");
            cJSON_DeleteItemFromObject(item, "subtype");
            cJSON_DeleteItemFromObject(item, "pos_x");
            cJSON_DeleteItemFromObject(item, "pos_y");
            cJSON_DeleteItemFromObject(item, "pos_z");
            cJSON_DeleteItemFromObject(item, "is_active");
            cJSON_DeleteItemFromObject(item, "updated_at");
            cJSON_DeleteItemFromObject(item, "synced");
            
            cJSON_AddStringToObject(item, "type", node->type);
            cJSON_AddStringToObject(item, "name", node->name);
            if (node->subtype[0]) cJSON_AddStringToObject(item, "subtype", node->subtype);
            cJSON_AddNumberToObject(item, "pos_x", node->pos_x);
            cJSON_AddNumberToObject(item, "pos_y", node->pos_y);
            cJSON_AddNumberToObject(item, "pos_z", node->pos_z);
            cJSON_AddBoolToObject(item, "is_active", node->is_active);
            cJSON_AddNumberToObject(item, "updated_at", node->updated_at);
            cJSON_AddBoolToObject(item, "synced", node->synced);
            
            cJSON_DeleteItemFromObject(item, "content");
            if (node->content) {
                cJSON *dup = cJSON_Duplicate(node->content, true);
                if (dup) {
                    cJSON_AddItemToObject(item, "content", dup);
                }
            }
            
            save_json_to_nvs(KEY_NODES, s_nodes);
            return ESP_OK;
        }
    }
    
    /* Add new if under limit */
    if (cJSON_GetArraySize(s_nodes) >= MAX_NODES) {
        ESP_LOGW(TAG, "Node cache full, removing oldest");
        cJSON_DeleteItemFromArray(s_nodes, 0);
    }
    
    cJSON *json = cJSON_CreateObject();
    if (!json) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(json, "id", node->id);
    cJSON_AddStringToObject(json, "tenant_id", node->tenant_id);
    cJSON_AddStringToObject(json, "type", node->type);
    cJSON_AddStringToObject(json, "name", node->name);
    if (node->subtype[0]) cJSON_AddStringToObject(json, "subtype", node->subtype);
    cJSON_AddNumberToObject(json, "pos_x", node->pos_x);
    cJSON_AddNumberToObject(json, "pos_y", node->pos_y);
    cJSON_AddNumberToObject(json, "pos_z", node->pos_z);
    cJSON_AddBoolToObject(json, "is_active", node->is_active);
    cJSON_AddNumberToObject(json, "updated_at", node->updated_at);
    cJSON_AddBoolToObject(json, "synced", node->synced);
    if (node->content) {
        cJSON *dup = cJSON_Duplicate(node->content, true);
        if (dup) {
            cJSON_AddItemToObject(json, "content", dup);
        }
    }
    
    cJSON_AddItemToArray(s_nodes, json);
    save_json_to_nvs(KEY_NODES, s_nodes);
    
    return ESP_OK;
}

esp_err_t neuron_cache_get_node(const char *id, cache_node_t **out_node) {
    if (!s_initialized || !id || !out_node) return ESP_ERR_INVALID_ARG;
    
    *out_node = NULL;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_nodes) {
        cJSON *node_id = cJSON_GetObjectItem(item, "id");
        if (node_id && strcmp(node_id->valuestring, id) == 0) {
            cache_node_t *node = calloc(1, sizeof(cache_node_t));
            if (!node) return ESP_ERR_NO_MEM;
            
            strncpy(node->id, id, 36);
            
            cJSON *t = cJSON_GetObjectItem(item, "tenant_id");
            if (t) strncpy(node->tenant_id, t->valuestring, 36);
            
            t = cJSON_GetObjectItem(item, "type");
            if (t) strncpy(node->type, t->valuestring, 31);
            
            t = cJSON_GetObjectItem(item, "name");
            if (t) strncpy(node->name, t->valuestring, 127);
            
            t = cJSON_GetObjectItem(item, "subtype");
            if (t) strncpy(node->subtype, t->valuestring, 63);
            
            cJSON *px = cJSON_GetObjectItem(item, "pos_x");
            cJSON *py = cJSON_GetObjectItem(item, "pos_y");
            cJSON *pz = cJSON_GetObjectItem(item, "pos_z");
            if (px) node->pos_x = px->valuedouble;
            if (py) node->pos_y = py->valuedouble;
            if (pz) node->pos_z = pz->valuedouble;
            
            cJSON *active = cJSON_GetObjectItem(item, "is_active");
            node->is_active = active && cJSON_IsTrue(active);
            
            cJSON *updated = cJSON_GetObjectItem(item, "updated_at");
            if (updated) node->updated_at = updated->valueint;
            
            cJSON *synced = cJSON_GetObjectItem(item, "synced");
            node->synced = synced && cJSON_IsTrue(synced);
            
            cJSON *content = cJSON_GetObjectItem(item, "content");
            if (content) node->content = cJSON_Duplicate(content, true);
            
            *out_node = node;
            return ESP_OK;
        }
    }
    
    return ESP_OK;
}

esp_err_t neuron_cache_delete_node(const char *id) {
    if (!s_initialized || !id) return ESP_ERR_INVALID_ARG;
    
    cJSON *item = NULL;
    int idx = 0;
    cJSON_ArrayForEach(item, s_nodes) {
        cJSON *node_id = cJSON_GetObjectItem(item, "id");
        if (node_id && strcmp(node_id->valuestring, id) == 0) {
            cJSON_DeleteItemFromArray(s_nodes, idx);
            save_json_to_nvs(KEY_NODES, s_nodes);
            return ESP_OK;
        }
        idx++;
    }
    
    return ESP_OK;
}

cache_node_t** neuron_cache_get_nodes_by_tenant(const char *tenant_id, uint32_t *out_count) {
    *out_count = 0;
    if (!s_initialized || !tenant_id) return NULL;
    
    cache_node_t **nodes = NULL;
    cache_node_t *node = NULL;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_nodes) {
        cJSON *tid = cJSON_GetObjectItem(item, "tenant_id");
        if (tid && strcmp(tid->valuestring, tenant_id) == 0) {
            if (neuron_cache_get_node(
                    cJSON_GetObjectItem(item, "id")->valuestring, &node) == ESP_OK && node) {
                nodes = realloc(nodes, (*out_count + 1) * sizeof(cache_node_t*));
                nodes[(*out_count)++] = node;
            }
        }
    }
    
    return nodes;
}

cache_node_t** neuron_cache_get_unsynced_nodes(uint32_t *out_count) {
    *out_count = 0;
    if (!s_initialized) return NULL;
    
    cache_node_t **nodes = NULL;
    cache_node_t *node = NULL;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_nodes) {
        cJSON *synced = cJSON_GetObjectItem(item, "synced");
        if (!synced || !cJSON_IsTrue(synced)) {
            if (neuron_cache_get_node(
                    cJSON_GetObjectItem(item, "id")->valuestring, &node) == ESP_OK && node) {
                nodes = realloc(nodes, (*out_count + 1) * sizeof(cache_node_t*));
                nodes[(*out_count)++] = node;
            }
        }
    }
    
    return nodes;
}

esp_err_t neuron_cache_mark_node_synced(const char *id) {
    if (!s_initialized || !id) return ESP_ERR_INVALID_ARG;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_nodes) {
        cJSON *node_id = cJSON_GetObjectItem(item, "id");
        if (node_id && strcmp(node_id->valuestring, id) == 0) {
            cJSON_DeleteItemFromObject(item, "synced");
            cJSON_AddBoolToObject(item, "synced", true);
            save_json_to_nvs(KEY_NODES, s_nodes);
            return ESP_OK;
        }
    }
    
    return ESP_OK;
}

/* ============================================================
 * Link Operations
 * ============================================================ */

esp_err_t neuron_cache_upsert_link(const cache_link_t *link) {
    if (!s_initialized || !link) return ESP_ERR_INVALID_ARG;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_links) {
        cJSON *id = cJSON_GetObjectItem(item, "id");
        if (id && strcmp(id->valuestring, link->id) == 0) {
            cJSON_DeleteItemFromObject(item, "source_id");
            cJSON_DeleteItemFromObject(item, "target_id");
            cJSON_DeleteItemFromObject(item, "type");
            cJSON_DeleteItemFromObject(item, "weight");
            cJSON_DeleteItemFromObject(item, "updated_at");
            cJSON_DeleteItemFromObject(item, "synced");
            
            cJSON_AddStringToObject(item, "source_id", link->source_id);
            cJSON_AddStringToObject(item, "target_id", link->target_id);
            cJSON_AddStringToObject(item, "type", link->type);
            cJSON_AddNumberToObject(item, "weight", link->weight);
            cJSON_AddNumberToObject(item, "updated_at", link->updated_at);
            cJSON_AddBoolToObject(item, "synced", link->synced);
            
            save_json_to_nvs(KEY_LINKS, s_links);
            return ESP_OK;
        }
    }
    
    if (cJSON_GetArraySize(s_links) >= MAX_LINKS) {
        ESP_LOGW(TAG, "Link cache full, removing oldest");
        cJSON_DeleteItemFromArray(s_links, 0);
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "id", link->id);
    cJSON_AddStringToObject(json, "tenant_id", link->tenant_id);
    cJSON_AddStringToObject(json, "source_id", link->source_id);
    cJSON_AddStringToObject(json, "target_id", link->target_id);
    cJSON_AddStringToObject(json, "type", link->type);
    cJSON_AddNumberToObject(json, "weight", link->weight);
    cJSON_AddNumberToObject(json, "updated_at", link->updated_at);
    cJSON_AddBoolToObject(json, "synced", link->synced);
    
    cJSON_AddItemToArray(s_links, json);
    save_json_to_nvs(KEY_LINKS, s_links);
    
    return ESP_OK;
}

esp_err_t neuron_cache_get_link(const char *id, cache_link_t **out_link) {
    if (!s_initialized || !id || !out_link) return ESP_ERR_INVALID_ARG;
    
    *out_link = NULL;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_links) {
        cJSON *link_id = cJSON_GetObjectItem(item, "id");
        if (link_id && strcmp(link_id->valuestring, id) == 0) {
            cache_link_t *link = calloc(1, sizeof(cache_link_t));
            if (!link) return ESP_ERR_NO_MEM;
            
            strncpy(link->id, id, 36);
            
            cJSON *t = cJSON_GetObjectItem(item, "tenant_id");
            if (t) strncpy(link->tenant_id, t->valuestring, 36);
            
            t = cJSON_GetObjectItem(item, "source_id");
            if (t) strncpy(link->source_id, t->valuestring, 36);
            
            t = cJSON_GetObjectItem(item, "target_id");
            if (t) strncpy(link->target_id, t->valuestring, 36);
            
            t = cJSON_GetObjectItem(item, "type");
            if (t) strncpy(link->type, t->valuestring, 31);
            
            cJSON *w = cJSON_GetObjectItem(item, "weight");
            if (w) link->weight = w->valuedouble;
            
            cJSON *updated = cJSON_GetObjectItem(item, "updated_at");
            if (updated) link->updated_at = updated->valueint;
            
            *out_link = link;
            return ESP_OK;
        }
    }
    
    return ESP_OK;
}

esp_err_t neuron_cache_delete_link(const char *id) {
    if (!s_initialized || !id) return ESP_ERR_INVALID_ARG;
    
    cJSON *item = NULL;
    int idx = 0;
    cJSON_ArrayForEach(item, s_links) {
        cJSON *link_id = cJSON_GetObjectItem(item, "id");
        if (link_id && strcmp(link_id->valuestring, id) == 0) {
            cJSON_DeleteItemFromArray(s_links, idx);
            save_json_to_nvs(KEY_LINKS, s_links);
            return ESP_OK;
        }
        idx++;
    }
    
    return ESP_OK;
}

cache_link_t** neuron_cache_get_links_by_tenant(const char *tenant_id, uint32_t *out_count) {
    *out_count = 0;
    if (!s_initialized || !tenant_id) return NULL;
    
    cache_link_t **links = NULL;
    cache_link_t *link = NULL;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_links) {
        cJSON *tid = cJSON_GetObjectItem(item, "tenant_id");
        if (tid && strcmp(tid->valuestring, tenant_id) == 0) {
            if (neuron_cache_get_link(
                    cJSON_GetObjectItem(item, "id")->valuestring, &link) == ESP_OK && link) {
                links = realloc(links, (*out_count + 1) * sizeof(cache_link_t*));
                links[(*out_count)++] = link;
            }
        }
    }
    
    return links;
}

cache_link_t** neuron_cache_get_links_by_source(const char *source_id, uint32_t *out_count) {
    *out_count = 0;
    if (!s_initialized || !source_id) return NULL;
    
    cache_link_t **links = NULL;
    cache_link_t *link = NULL;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_links) {
        cJSON *sid = cJSON_GetObjectItem(item, "source_id");
        if (sid && strcmp(sid->valuestring, source_id) == 0) {
            if (neuron_cache_get_link(
                    cJSON_GetObjectItem(item, "id")->valuestring, &link) == ESP_OK && link) {
                links = realloc(links, (*out_count + 1) * sizeof(cache_link_t*));
                links[(*out_count)++] = link;
            }
        }
    }
    
    return links;
}

cache_link_t** neuron_cache_get_unsynced_links(uint32_t *out_count) {
    *out_count = 0;
    if (!s_initialized) return NULL;
    
    cache_link_t **links = NULL;
    cache_link_t *link = NULL;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_links) {
        cJSON *synced = cJSON_GetObjectItem(item, "synced");
        if (!synced || !cJSON_IsTrue(synced)) {
            if (neuron_cache_get_link(
                    cJSON_GetObjectItem(item, "id")->valuestring, &link) == ESP_OK && link) {
                links = realloc(links, (*out_count + 1) * sizeof(cache_link_t*));
                links[(*out_count)++] = link;
            }
        }
    }
    
    return links;
}

esp_err_t neuron_cache_mark_link_synced(const char *id) {
    if (!s_initialized || !id) return ESP_ERR_INVALID_ARG;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_links) {
        cJSON *link_id = cJSON_GetObjectItem(item, "id");
        if (link_id && strcmp(link_id->valuestring, id) == 0) {
            cJSON_DeleteItemFromObject(item, "synced");
            cJSON_AddBoolToObject(item, "synced", true);
            save_json_to_nvs(KEY_LINKS, s_links);
            return ESP_OK;
        }
    }
    
    return ESP_OK;
}

/* ============================================================
 * Pulse Operations
 * ============================================================ */

esp_err_t neuron_cache_insert_pulse(const cache_pulse_t *pulse) {
    if (!s_initialized || !pulse) return ESP_ERR_INVALID_ARG;
    
    if (cJSON_GetArraySize(s_pulses) >= MAX_PULSES) {
        ESP_LOGW(TAG, "Pulse cache full, removing oldest");
        cJSON_DeleteItemFromArray(s_pulses, 0);
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "id", pulse->id);
    cJSON_AddStringToObject(json, "tenant_id", pulse->tenant_id);
    cJSON_AddStringToObject(json, "device_id", pulse->device_id);
    cJSON_AddStringToObject(json, "type", pulse->type);
    if (pulse->source_id[0]) cJSON_AddStringToObject(json, "source_id", pulse->source_id);
    if (pulse->target_id[0]) cJSON_AddStringToObject(json, "target_id", pulse->target_id);
    if (pulse->text) cJSON_AddStringToObject(json, "text", pulse->text);
    cJSON_AddNumberToObject(json, "energy", pulse->energy);
    cJSON_AddNumberToObject(json, "created_at", pulse->created_at);
    cJSON_AddBoolToObject(json, "synced", pulse->synced);
    
    cJSON_AddItemToArray(s_pulses, json);
    save_json_to_nvs(KEY_PULSES, s_pulses);
    
    return ESP_OK;
}

cache_pulse_t** neuron_cache_get_pulses_by_tenant(const char *tenant_id, uint32_t *out_count) {
    *out_count = 0;
    if (!s_initialized || !tenant_id) return NULL;
    
    cache_pulse_t **pulses = NULL;
    cache_pulse_t *pulse = NULL;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_pulses) {
        cJSON *tid = cJSON_GetObjectItem(item, "tenant_id");
        if (tid && strcmp(tid->valuestring, tenant_id) == 0) {
            pulse = calloc(1, sizeof(cache_pulse_t));
            if (!pulse) break;
            
            cJSON *id = cJSON_GetObjectItem(item, "id");
            if (id) strncpy(pulse->id, id->valuestring, 36);
            
            cJSON *t = cJSON_GetObjectItem(item, "device_id");
            if (t) strncpy(pulse->device_id, t->valuestring, 36);
            
            t = cJSON_GetObjectItem(item, "type");
            if (t) strncpy(pulse->type, t->valuestring, 31);
            
            t = cJSON_GetObjectItem(item, "source_id");
            if (t) strncpy(pulse->source_id, t->valuestring, 36);
            
            t = cJSON_GetObjectItem(item, "target_id");
            if (t) strncpy(pulse->target_id, t->valuestring, 36);
            
            t = cJSON_GetObjectItem(item, "text");
            if (t && t->valuestring) pulse->text = strdup(t->valuestring);
            
            cJSON *e = cJSON_GetObjectItem(item, "energy");
            if (e) pulse->energy = e->valuedouble;
            
            cJSON *c = cJSON_GetObjectItem(item, "created_at");
            if (c) pulse->created_at = c->valueint;
            
            pulses = realloc(pulses, (*out_count + 1) * sizeof(cache_pulse_t*));
            pulses[(*out_count)++] = pulse;
        }
    }
    
    return pulses;
}

cache_pulse_t** neuron_cache_get_pulses_by_node(const char *node_id, uint32_t *out_count) {
    *out_count = 0;
    if (!s_initialized || !node_id) return NULL;
    
    cache_pulse_t **pulses = NULL;
    cache_pulse_t *pulse = NULL;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_pulses) {
        cJSON *sid = cJSON_GetObjectItem(item, "source_id");
        cJSON *tid = cJSON_GetObjectItem(item, "target_id");
        
        if ((sid && strcmp(sid->valuestring, node_id) == 0) ||
            (tid && strcmp(tid->valuestring, node_id) == 0)) {
            
            pulse = calloc(1, sizeof(cache_pulse_t));
            if (!pulse) break;
            
            cJSON *id = cJSON_GetObjectItem(item, "id");
            if (id) strncpy(pulse->id, id->valuestring, 36);
            
            cJSON *t = cJSON_GetObjectItem(item, "type");
            if (t) strncpy(pulse->type, t->valuestring, 31);
            
            pulses = realloc(pulses, (*out_count + 1) * sizeof(cache_pulse_t*));
            pulses[(*out_count)++] = pulse;
        }
    }
    
    return pulses;
}

cache_pulse_t** neuron_cache_get_unsynced_pulses(uint32_t *out_count) {
    *out_count = 0;
    if (!s_initialized) return NULL;
    
    cache_pulse_t **pulses = NULL;
    cache_pulse_t *pulse = NULL;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_pulses) {
        cJSON *synced = cJSON_GetObjectItem(item, "synced");
        if (!synced || !cJSON_IsTrue(synced)) {
            pulse = calloc(1, sizeof(cache_pulse_t));
            if (!pulse) break;
            
            cJSON *id = cJSON_GetObjectItem(item, "id");
            if (id) strncpy(pulse->id, id->valuestring, 36);
            
            cJSON *t = cJSON_GetObjectItem(item, "tenant_id");
            if (t) strncpy(pulse->tenant_id, t->valuestring, 36);
            
            t = cJSON_GetObjectItem(item, "device_id");
            if (t) strncpy(pulse->device_id, t->valuestring, 36);
            
            pulses = realloc(pulses, (*out_count + 1) * sizeof(cache_pulse_t*));
            pulses[(*out_count)++] = pulse;
        }
    }
    
    return pulses;
}

esp_err_t neuron_cache_mark_pulse_synced(const char *id) {
    if (!s_initialized || !id) return ESP_ERR_INVALID_ARG;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, s_pulses) {
        cJSON *pulse_id = cJSON_GetObjectItem(item, "id");
        if (pulse_id && strcmp(pulse_id->valuestring, id) == 0) {
            cJSON_DeleteItemFromObject(item, "synced");
            cJSON_AddBoolToObject(item, "synced", true);
            save_json_to_nvs(KEY_PULSES, s_pulses);
            return ESP_OK;
        }
    }
    
    return ESP_OK;
}

/* ============================================================
 * Sync Operations
 * ============================================================ */

esp_err_t neuron_cache_set_last_sync(uint64_t timestamp) {
    if (!s_initialized) return ESP_ERR_INVALID_ARG;
    
    s_last_sync = timestamp;
    nvs_set_u64(s_nvs_handle, KEY_LAST_SYNC, timestamp);
    
    return ESP_OK;
}

uint64_t neuron_cache_get_last_sync(void) {
    return s_last_sync;
}

esp_err_t neuron_cache_reset(void) {
    if (!s_initialized) return ESP_ERR_INVALID_ARG;
    
    cJSON_Delete(s_nodes);
    cJSON_Delete(s_links);
    cJSON_Delete(s_pulses);
    
    s_nodes = cJSON_CreateArray();
    s_links = cJSON_CreateArray();
    s_pulses = cJSON_CreateArray();
    s_last_sync = 0;
    
    save_json_to_nvs(KEY_NODES, s_nodes);
    save_json_to_nvs(KEY_LINKS, s_links);
    save_json_to_nvs(KEY_PULSES, s_pulses);
    nvs_set_u64(s_nvs_handle, KEY_LAST_SYNC, 0);
    
    return ESP_OK;
}

/* ============================================================
 * Memory Management
 * ============================================================ */

void neuron_cache_free_node(cache_node_t *node) {
    if (node) {
        if (node->embedding) free(node->embedding);
        if (node->content) cJSON_Delete(node->content);
        free(node);
    }
}

void neuron_cache_free_link(cache_link_t *link) {
    if (link) {
        if (link->metadata) cJSON_Delete(link->metadata);
        free(link);
    }
}

void neuron_cache_free_pulse(cache_pulse_t *pulse) {
    if (pulse) {
        if (pulse->text) free(pulse->text);
        free(pulse);
    }
}

void neuron_cache_free_nodes(cache_node_t **nodes, uint32_t count) {
    if (nodes) {
        for (uint32_t i = 0; i < count; i++) {
            neuron_cache_free_node(nodes[i]);
        }
        free(nodes);
    }
}

void neuron_cache_free_links(cache_link_t **links, uint32_t count) {
    if (links) {
        for (uint32_t i = 0; i < count; i++) {
            neuron_cache_free_link(links[i]);
        }
        free(links);
    }
}

void neuron_cache_free_pulses(cache_pulse_t **pulses, uint32_t count) {
    if (pulses) {
        for (uint32_t i = 0; i < count; i++) {
            neuron_cache_free_pulse(pulses[i]);
        }
        free(pulses);
    }
}
