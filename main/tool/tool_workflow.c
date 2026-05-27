/*
 * ESPClaw - tool/tool_workflow.c
 *
 * Workflow management tools:
 * - workflow_list:   List all registered workflows
 * - workflow_enable: Enable a workflow by name
 * - workflow_disable: Disable a workflow by name
 * - workflow_sync:   Sync workflows from cloud
 */

#include "tool.h"
#include "workflow/workflow_engine.h"
#include "net/neuron_link.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "tool_wf";

/* ── workflow_list ──────────────────────────────────────────── */

static bool tool_workflow_list(const char *params, char *out, size_t out_sz)
{
    (void)params;

    int count = workflow_engine_count();
    if (count == 0) {
        snprintf(out, out_sz, "No workflows registered.");
        return true;
    }

    size_t pos = 0;
    pos += snprintf(out + pos, out_sz - pos, "%d workflow(s):\n", count);

    char list_buf[512];
    int len = workflow_engine_list_json(list_buf, sizeof(list_buf));
    if (len > 0) {
        /* Parse JSON array and display human-readable list */
        const char *p = list_buf;
        int idx = 0;
        while (*p && idx < count) {
            /* Skip to next { */
            while (*p && *p != '{') p++;
            if (!*p) break;

            /* Extract name and priority */
            const char *name_start = strstr(p, "\"name\":\"");
            const char *pri_start = strstr(p, "\"priority\":");
            const char *ena_start = strstr(p, "\"enabled\":");

            if (name_start) {
                const char *n = name_start + 8;
                const char *n_end = n;
                while (*n_end && *n_end != '"') n_end++;
                char name[64] = {0};
                size_t nlen = n_end - n;
                if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
                memcpy(name, n, nlen);

                int pri = pri_start ? atoi(pri_start + 10) : 50;
                bool ena = ena_start ? (ena_start[9] == 't') : true;

                pos += snprintf(out + pos, out_sz - pos,
                              "  %d. %s [pri=%d, %s]\n",
                              idx + 1, name, pri, ena ? "ON" : "OFF");
                idx++;
            }

            /* Move past this object */
            while (*p && *p != '}') p++;
            if (*p) p++;
        }
    }

    return true;
}

/* ── workflow_enable ──────────────────────────────────────── */

static bool tool_workflow_enable(const char *params, char *out, size_t out_sz)
{
    char name[WORKFLOW_MAX_NAME_LEN] = {0};

    /* Parse: { "name": "Morning Routine" } */
    const char *n = strstr(params, "\"name\":\"");
    if (!n) {
        /* Try bare word */
        snprintf(name, sizeof(name), "%s", params);
        // Trim whitespace
        char *end = name + strlen(name) - 1;
        while (end > name && isspace((unsigned char)*end)) *end-- = '\0';
    } else {
        const char *start = n + 8;
        const char *end = start;
        while (*end && *end != '"') end++;
        size_t len = end - start;
        if (len >= sizeof(name)) len = sizeof(name) - 1;
        memcpy(name, start, len);
    }

    if (!name[0]) {
        snprintf(out, out_sz, "Error: missing 'name' parameter");
        return false;
    }

    esp_err_t err = workflow_engine_set_enabled(name, true);
    if (err == ESP_OK) {
        snprintf(out, out_sz, "Workflow '%s' enabled.", name);
        return true;
    } else if (err == ESP_ERR_NOT_FOUND) {
        snprintf(out, out_sz, "Workflow '%s' not found.", name);
        return false;
    } else {
        snprintf(out, out_sz, "Error enabling workflow: %d", err);
        return false;
    }
}

/* ── workflow_disable ─────────────────────────────────────── */

static bool tool_workflow_disable(const char *params, char *out, size_t out_sz)
{
    char name[WORKFLOW_MAX_NAME_LEN] = {0};

    const char *n = strstr(params, "\"name\":\"");
    if (!n) {
        snprintf(name, sizeof(name), "%s", params);
        char *end = name + strlen(name) - 1;
        while (end > name && isspace((unsigned char)*end)) *end-- = '\0';
    } else {
        const char *start = n + 8;
        const char *end = start;
        while (*end && *end != '"') end++;
        size_t len = end - start;
        if (len >= sizeof(name)) len = sizeof(name) - 1;
        memcpy(name, start, len);
    }

    if (!name[0]) {
        snprintf(out, out_sz, "Error: missing 'name' parameter");
        return false;
    }

    esp_err_t err = workflow_engine_set_enabled(name, false);
    if (err == ESP_OK) {
        snprintf(out, out_sz, "Workflow '%s' disabled.", name);
        return true;
    } else if (err == ESP_ERR_NOT_FOUND) {
        snprintf(out, out_sz, "Workflow '%s' not found.", name);
        return false;
    } else {
        snprintf(out, out_sz, "Error disabling workflow: %d", err);
        return false;
    }
}

/* ── workflow_sync ────────────────────────────────────────── */

static bool tool_workflow_sync(const char *params, char *out, size_t out_sz)
{
    (void)params;

    esp_err_t err = neuron_link_sync_workflows();
    if (err == ESP_OK) {
        extern int workflow_engine_count(void);
        int count = workflow_engine_count();
        snprintf(out, out_sz, "Synced %d workflow(s) from cloud.", count);
        return true;
    } else if (err == ESP_ERR_INVALID_STATE) {
        snprintf(out, out_sz, "Error: device not paired. Pair first.");
        return false;
    } else {
        snprintf(out, out_sz, "Sync error: %d", err);
        return false;
    }
}

/* ── Public dispatch ───────────────────────────────────────── */

bool tool_workflow_dispatch(const char *tool_name, const char *params,
                          char *out, size_t out_sz)
{
    if (strcmp(tool_name, "workflow_list") == 0) {
        return tool_workflow_list(params, out, out_sz);
    } else if (strcmp(tool_name, "workflow_enable") == 0) {
        return tool_workflow_enable(params, out, out_sz);
    } else if (strcmp(tool_name, "workflow_disable") == 0) {
        return tool_workflow_disable(params, out, out_sz);
    } else if (strcmp(tool_name, "workflow_sync") == 0) {
        return tool_workflow_sync(params, out, out_sz);
    }
    snprintf(out, out_sz, "Unknown workflow tool: %s", tool_name);
    return false;
}
