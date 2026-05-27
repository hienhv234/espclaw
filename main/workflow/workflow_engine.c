/*
 * ESPClaw - workflow/workflow_engine.c
 *
 * Workflow Execution Engine for ESP32
 *
 * Design goals:
 * - Zero dynamic allocation (all static buffers)
 * - ~8KB total memory footprint
 * - Works without PSRAM
 * - Sync-compatible with Supabase workflow schema
 */

#include "workflow_engine.h"
#include "tool/tool_registry.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#include <inttypes.h>

static const char *TAG = "workflow";

// ── Storage ─────────────────────────────────────────────────────────

static EXT_RAM_BSS_ATTR workflow_t s_workflows[WORKFLOW_MAX_COUNT];
static int s_workflow_count = 0;

// Current execution state (one at a time)
static wf_exec_t s_exec = {
    .workflow = NULL,
    .current_step = 0,
    .status = WF_EXEC_IDLE,
};

// ── Pattern Matching ──────────────────────────────────────────────────

bool workflow_pattern_match(const char *pattern, const char *input)
{
    if (!pattern || !input) return false;

    // Regex mode: pattern starts with ^
    if (pattern[0] == '^') {
        // Simple regex: only supports ^prefix and suffix$ for now
        size_t pat_len = strlen(pattern);
        size_t inp_len = strlen(input);

        if (pat_len > 1 && pattern[pat_len - 1] == '$') {
            // Prefix match: ^hello$ means exact match
            if (pat_len == inp_len + 1) {
                return strncasecmp(pattern + 1, input, inp_len) == 0;
            }
            // Prefix match: ^hello matches start
            return strncasecmp(pattern + 1, input, pat_len - 1) == 0;
        } else {
            // Just ^prefix
            return strncasecmp(pattern + 1, input, pat_len - 1) == 0;
        }
    }

    // Contains match (case-insensitive)
    size_t pat_len = strlen(pattern);
    size_t inp_len = strlen(input);
    if (pat_len > inp_len) return false;

    for (size_t i = 0; i <= inp_len - pat_len; i++) {
        if (strncasecmp(pattern, input + i, pat_len) == 0) {
            return true;
        }
    }
    return false;
}

// ── JSON Parsing Helpers ─────────────────────────────────────────────

/* Find value in JSON object by key (minimal parser, no external deps) */
static const char *json_get_str(const char *json, const char *key, char *out, size_t out_sz)
{
    if (!json || !key) return NULL;
    char key_buf[64];
    snprintf(key_buf, sizeof(key_buf), "\"%s\"", key);

    const char *p = strstr(json, key_buf);
    if (!p) return NULL;

    p = strchr(p, ':');
    if (!p) return NULL;
    p++;

    // Skip whitespace
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;

    if (*p != '"') return NULL; // Not a string
    p++;

    const char *end = p;
    while (*end && *end != '"' && (end - p) < (ssize_t)out_sz - 1) end++;
    size_t len = end - p;
    if (len >= out_sz) len = out_sz - 1;

    memcpy(out, p, len);
    out[len] = '\0';
    return out;
}

static int json_get_int(const char *json, const char *key, int default_val)
{
    char buf[32];
    if (!json_get_str(json, key, buf, sizeof(buf))) return default_val;
    return atoi(buf);
}

static bool json_get_bool(const char *json, const char *key, bool default_val)
{
    char buf[16];
    if (!json_get_str(json, key, buf, sizeof(buf))) return default_val;
    if (strcmp(buf, "true") == 0 || strcmp(buf, "1") == 0) return true;
    if (strcmp(buf, "false") == 0 || strcmp(buf, "0") == 0) return false;
    return default_val;
}

/* Copy a JSON substring safely */
static size_t json_copy_str(const char *src, char *dst, size_t dst_sz)
{
    if (!src || !dst || dst_sz == 0) return 0;
    const char *start = src;
    const char *end = src;
    bool in_string = false;

    while (*end && (size_t)(end - start) < dst_sz - 1) {
        if (*end == '"' && (end == start || *(end-1) != '\\')) {
            in_string = !in_string;
        }
        if (!in_string && (*end == ',' || *end == '}')) {
            break;
        }
        *dst++ = *end;
        end++;
    }
    *dst = '\0';
    return end - start;
}

/* Parse a workflow step from JSON */
static bool parse_step(const char *step_json, wf_step_t *out_step)
{
    if (!step_json || !out_step) return false;

    char type_buf[16];
    if (!json_get_str(step_json, "type", type_buf, sizeof(type_buf))) return false;

    out_step->id = (uint8_t)json_get_int(step_json, "id", 0);

    if (strcmp(type_buf, "tool") == 0) {
        out_step->type = WF_STEP_TOOL;
        json_get_str(step_json, "tool", out_step->tool_name, sizeof(out_step->tool_name));

        // Extract params object
        const char *params_start = strstr(step_json, "\"params\"");
        if (params_start) {
            const char *colon = strchr(params_start, ':');
            if (colon) {
                colon++;
                while (*colon == ' ' || *colon == '\t' || *colon == '\n') colon++;
                json_copy_str(colon, out_step->params_json, sizeof(out_step->params_json));
            }
        }
    } else if (strcmp(type_buf, "wait") == 0) {
        out_step->type = WF_STEP_WAIT;
        out_step->wait_ms = (uint32_t)json_get_int(step_json, "milliseconds", 500);
    }

    return true;
}

/* Parse a workflow from JSON */
static bool parse_workflow(const char *wf_json, workflow_t *out_wf)
{
    if (!wf_json || !out_wf) return false;

    memset(out_wf, 0, sizeof(workflow_t));

    if (!json_get_str(wf_json, "name", out_wf->name, sizeof(out_wf->name))) {
        return false;
    }

    // Trigger type
    char trigger_buf[24];
    if (json_get_str(wf_json, "trigger_type", trigger_buf, sizeof(trigger_buf))) {
        if (strcmp(trigger_buf, "voice_command") == 0) out_wf->trigger_type = WF_TRIGGER_VOICE;
        else if (strcmp(trigger_buf, "scheduled") == 0) out_wf->trigger_type = WF_TRIGGER_SCHEDULED;
        else if (strcmp(trigger_buf, "event") == 0) out_wf->trigger_type = WF_TRIGGER_EVENT;
        else out_wf->trigger_type = WF_TRIGGER_MANUAL;
    }

    // Trigger pattern
    json_get_str(wf_json, "trigger_pattern", out_wf->trigger_pattern, sizeof(out_wf->trigger_pattern));

    // Priority
    out_wf->priority = (uint8_t)json_get_int(wf_json, "priority", 50);
    out_wf->is_enabled = json_get_bool(wf_json, "is_enabled", true);

    // Parse steps array
    const char *steps_start = strstr(wf_json, "\"steps\"");
    if (!steps_start) return true; // No steps = valid

    const char *bracket = strchr(steps_start, '[');
    if (!bracket) return true;

    int depth = 1;
    const char *step_start = bracket + 1;
    const char *p = step_start;
    wf_step_t step;
    int step_idx = 0;

    while (*p && depth > 0 && step_idx < WORKFLOW_MAX_STEPS) {
        if (*p == '[') depth++;
        else if (*p == ']') depth--;
        else if (*p == '{' && depth == 1) {
            // Found step object
            const char *obj_start = p;
            int obj_depth = 1;
            const char *obj_end = p + 1;
            while (*obj_end && obj_depth > 0) {
                if (*obj_end == '{') obj_depth++;
                else if (*obj_end == '}') obj_depth--;
                obj_end++;
            }

            // Null-terminate the object for parsing
            char obj_buf[WORKFLOW_MAX_PARAMS_JSON];
            size_t obj_len = obj_end - obj_start;
            if (obj_len < sizeof(obj_buf)) {
                memcpy(obj_buf, obj_start, obj_len);
                obj_buf[obj_len] = '\0';

                if (parse_step(obj_buf, &step)) {
                    out_wf->steps[step_idx++] = step;
                }
            }
            p = obj_end;
            continue;
        }
        p++;
    }

    out_wf->step_count = step_idx;
    return true;
}

// ── Public API Implementation ──────────────────────────────────────────

esp_err_t workflow_engine_init(void)
{
    memset(s_workflows, 0, sizeof(s_workflows));
    s_workflow_count = 0;
    memset(&s_exec, 0, sizeof(s_exec));
    s_exec.status = WF_EXEC_IDLE;
    ESP_LOGI(TAG, "Workflow engine initialized (max %d workflows)", WORKFLOW_MAX_COUNT);
    return ESP_OK;
}

esp_err_t workflow_engine_load(const char *json_workflows, size_t json_len)
{
    if (!json_workflows) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Loading workflows from JSON (%d bytes)", json_len);

    int count = 0;
    const char *p = json_workflows;
    int depth = 1;
    const char *obj_start = NULL;

    while (*p && count < WORKFLOW_MAX_COUNT) {
        if (*p == '[' && obj_start == NULL) {
            depth = 1;
        } else if (*p == '{' && depth == 1) {
            obj_start = p;
        } else if (*p == '}' && obj_start != NULL) {
            depth--;
            if (depth == 0) {
                char buf[1024];
                size_t obj_len = p - obj_start + 1;
                if (obj_len < sizeof(buf)) {
                    memcpy(buf, obj_start, obj_len);
                    buf[obj_len] = '\0';

                    workflow_t wf;
                    if (parse_workflow(buf, &wf)) {
                        s_workflows[count++] = wf;
                    }
                }
                obj_start = NULL;
                depth = 1;
            }
        } else if (*p == '[') {
            depth++;
        } else if (*p == ']') {
            depth--;
        }
        p++;
    }

    s_workflow_count = count;
    ESP_LOGI(TAG, "Loaded %d workflows", count);

    // Log workflow names
    for (int i = 0; i < count; i++) {
        ESP_LOGI(TAG, "  [%d] %s (priority=%d, steps=%d, pattern=\"%s\")",
                 i, s_workflows[i].name, s_workflows[i].priority,
                 s_workflows[i].step_count, s_workflows[i].trigger_pattern);
    }

    return ESP_OK;
}

esp_err_t workflow_engine_add(const workflow_t *wf)
{
    if (!wf || s_workflow_count >= WORKFLOW_MAX_COUNT) {
        return ESP_ERR_NO_MEM;
    }

    // Check for duplicate name
    for (int i = 0; i < s_workflow_count; i++) {
        if (strcmp(s_workflows[i].name, wf->name) == 0) {
            s_workflows[i] = *wf; // Update existing
            ESP_LOGI(TAG, "Updated workflow: %s", wf->name);
            return ESP_OK;
        }
    }

    s_workflows[s_workflow_count++] = *wf;
    ESP_LOGI(TAG, "Added workflow: %s (total: %d)", wf->name, s_workflow_count);
    return ESP_OK;
}

esp_err_t workflow_engine_remove(const char *name)
{
    if (!name) return ESP_ERR_INVALID_ARG;

    for (int i = 0; i < s_workflow_count; i++) {
        if (strcmp(s_workflows[i].name, name) == 0) {
            // Shift remaining workflows
            for (int j = i; j < s_workflow_count - 1; j++) {
                s_workflows[j] = s_workflows[j + 1];
            }
            s_workflow_count--;
            ESP_LOGI(TAG, "Removed workflow: %s", name);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

const workflow_t *workflow_engine_match(const char *input_text)
{
    wf_match_t matches[WORKFLOW_MAX_COUNT];
    int count = workflow_engine_match_all(input_text, matches, WORKFLOW_MAX_COUNT);
    return count > 0 ? matches[0].workflow : NULL;
}

int workflow_engine_match_all(const char *input_text,
                            wf_match_t *matches,
                            int max_count)
{
    if (!input_text || !matches || max_count == 0) return 0;

    int match_count = 0;

    for (int i = 0; i < s_workflow_count && match_count < max_count; i++) {
        const workflow_t *wf = &s_workflows[i];

        if (!wf->is_enabled) continue;
        if (wf->trigger_type != WF_TRIGGER_VOICE) continue;
        if (!wf->trigger_pattern[0]) continue;

        if (workflow_pattern_match(wf->trigger_pattern, input_text)) {
            matches[match_count].workflow = wf;
            matches[match_count].match_score = 1.0f;
            match_count++;
        }
    }

    // Sort by priority (simple bubble for small arrays)
    for (int i = 0; i < match_count - 1; i++) {
        for (int j = i + 1; j < match_count; j++) {
            if (matches[j].workflow->priority > matches[i].workflow->priority) {
                wf_match_t tmp = matches[i];
                matches[i] = matches[j];
                matches[j] = tmp;
            }
        }
    }

    return match_count;
}

esp_err_t workflow_engine_execute(const workflow_t *wf,
                                const char *input_text,
                                wf_exec_t *out_exec)
{
    if (!wf || !out_exec) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Executing workflow: %s (%d steps)", wf->name, wf->step_count);

    // Init execution state
    memset(out_exec, 0, sizeof(wf_exec_t));
    out_exec->workflow = wf;
    out_exec->status = WF_EXEC_RUNNING;
    out_exec->started_at = xTaskGetTickCount() * (1000 / configTICK_RATE_HZ);

    // Also update global state
    memcpy(&s_exec, out_exec, sizeof(wf_exec_t));

    char result_buf[WORKFLOW_RESULT_BUF_SIZE];
    size_t result_len = 0;

    for (uint8_t i = 0; i < wf->step_count; i++) {
        const wf_step_t *step = &wf->steps[i];

        out_exec->current_step = i;
        s_exec.current_step = i;

        if (step->type == WF_STEP_TOOL) {
            ESP_LOGI(TAG, "  Step %d: calling tool '%s'", step->id, step->tool_name);

            char tool_result[256];
            memset(tool_result, 0, sizeof(tool_result));

            bool ok = tool_registry_dispatch(
                step->tool_name,
                step->params_json[0] ? step->params_json : "{}",
                tool_result,
                sizeof(tool_result)
            );

            out_exec->tool_calls++;
            s_exec.tool_calls++;

            // Append to result
            size_t written = snprintf(result_buf + result_len,
                                     sizeof(result_buf) - result_len,
                                     "[%d] %s: %s\n",
                                     step->id, step->tool_name, tool_result);
            if (written < sizeof(result_buf) - result_len) {
                result_len += written;
            }

            if (!ok) {
                ESP_LOGW(TAG, "  Step %d FAILED: %s", step->id, tool_result);
                out_exec->status = WF_EXEC_FAILED;
                s_exec.status = WF_EXEC_FAILED;
                snprintf(out_exec->error_msg, sizeof(out_exec->error_msg),
                         "Tool '%.32s' failed: %.64s", step->tool_name, tool_result);
                goto done;
            }

        } else if (step->type == WF_STEP_WAIT) {
            uint32_t wait_ms = step->wait_ms;
            if (wait_ms > 5000) wait_ms = 5000; // Cap at 5s for safety
            ESP_LOGI(TAG, "  Step %d: wait %" PRIu32 "ms", step->id, wait_ms);
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
        }
    }

    out_exec->status = WF_EXEC_COMPLETED;
    s_exec.status = WF_EXEC_COMPLETED;

done:
    uint32_t now_tick = xTaskGetTickCount();
    uint32_t now_ms = now_tick * (1000 / configTICK_RATE_HZ);
    out_exec->duration_ms = now_ms - out_exec->started_at;
    s_exec.duration_ms = out_exec->duration_ms;

    // Copy result
    size_t cpylen = result_len < sizeof(out_exec->result_buf) - 1
                        ? result_len : sizeof(out_exec->result_buf) - 1;
    memcpy(out_exec->result_buf, result_buf, cpylen);
    out_exec->result_buf[cpylen] = '\0';
    memcpy(s_exec.result_buf, result_buf, cpylen);
    s_exec.result_buf[cpylen] = '\0';

    ESP_LOGI(TAG, "Workflow %s completed in %" PRIu32 "ms: %s",
             wf->name, out_exec->duration_ms,
             out_exec->status == WF_EXEC_COMPLETED ? "OK" : "FAILED");

    return ESP_OK;
}

int workflow_engine_count(void)
{
    return s_workflow_count;
}

const workflow_t *workflow_engine_get(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < s_workflow_count; i++) {
        if (strcmp(s_workflows[i].name, name) == 0) {
            return &s_workflows[i];
        }
    }
    return NULL;
}

esp_err_t workflow_engine_set_enabled(const char *name, bool enabled)
{
    workflow_t *wf = (workflow_t *)workflow_engine_get(name);
    if (!wf) return ESP_ERR_NOT_FOUND;
    wf->is_enabled = enabled;
    ESP_LOGI(TAG, "Workflow '%s' %s", name, enabled ? "enabled" : "disabled");
    return ESP_OK;
}

const wf_exec_t *workflow_engine_get_exec_state(void)
{
    return &s_exec;
}

esp_err_t workflow_engine_abort(void)
{
    if (s_exec.status == WF_EXEC_RUNNING) {
        s_exec.status = WF_EXEC_FAILED;
        snprintf(s_exec.error_msg, sizeof(s_exec.error_msg), "Aborted by user");
        ESP_LOGW(TAG, "Workflow aborted: %s", s_exec.workflow ? s_exec.workflow->name : "?");
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

void workflow_engine_mem_info(uint32_t *used, uint32_t *free)
{
    uint32_t heap_free = esp_get_free_heap_size();

    // Estimate our usage
    uint32_t our_used = sizeof(s_workflows) + sizeof(s_exec);
    if (used) *used = our_used;
    if (free) *free = heap_free;
}

int workflow_engine_list_json(char *out_buf, size_t out_sz)
{
    if (!out_buf || out_sz < 16) return -1;

    size_t pos = 0;
    pos += snprintf(out_buf + pos, out_sz - pos, "[");

    for (int i = 0; i < s_workflow_count; i++) {
        const workflow_t *wf = &s_workflows[i];
        pos += snprintf(out_buf + pos, out_sz - pos,
                        "%s{\"name\":\"%s\",\"priority\":%d,"
                        "\"steps\":%d,\"enabled\":%s}",
                        i > 0 ? "," : "",
                        wf->name, wf->priority, wf->step_count,
                        wf->is_enabled ? "true" : "false");
    }

    pos += snprintf(out_buf + pos, out_sz - pos, "]");
    return (int)pos;
}
