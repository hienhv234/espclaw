/*
 * ESPClaw - workflow/workflow_engine.h
 *
 * Workflow Execution Engine for ESP32
 *
 * Responsibilities:
 * - Load workflows from JSON (synced from cloud)
 * - Match incoming user input against workflow trigger patterns
 * - Execute matched workflow steps (tool calls)
 * - Fall back to LLM if no workflow matches
 *
 * Memory budget: ~8KB for workflow state (no PSRAM)
 */

#ifndef WORKFLOW_ENGINE_H
#define WORKFLOW_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Constants ────────────────────────────────────────────────────────────

#define WORKFLOW_MAX_COUNT       20
#define WORKFLOW_MAX_NAME_LEN    48
#define WORKFLOW_MAX_STEPS       16
#define WORKFLOW_MAX_TOOL_NAME  24
#define WORKFLOW_MAX_PARAMS_JSON 512
#define WORKFLOW_RESULT_BUF_SIZE  768
#define WORKFLOW_INPUT_BUF_SIZE  256

// ── Trigger Types ────────────────────────────────────────────────────────

typedef enum {
    WF_TRIGGER_VOICE,      // Text/regex match on voice input
    WF_TRIGGER_SCHEDULED,   // Cron-like scheduling
    WF_TRIGGER_EVENT,      // Triggered by another workflow
    WF_TRIGGER_MANUAL,     // Triggered from dashboard
} wf_trigger_type_t;

// ── Step Types ──────────────────────────────────────────────────────────

typedef enum {
    WF_STEP_TOOL,   // Call a registered ESP32 tool
    WF_STEP_WAIT,    // Delay/wait
} wf_step_type_t;

// ── Step Definition ────────────────────────────────────────────────────

typedef struct {
    uint8_t         id;
    wf_step_type_t  type;
    char            tool_name[WORKFLOW_MAX_TOOL_NAME];  // For type=TOOL
    char            params_json[WORKFLOW_MAX_PARAMS_JSON]; // JSON params for tool
    uint32_t        wait_ms;                            // For type=WAIT
} wf_step_t;

// ── Workflow Definition ────────────────────────────────────────────────

typedef struct {
    char            name[WORKFLOW_MAX_NAME_LEN];
    wf_trigger_type_t trigger_type;
    char            trigger_pattern[WORKFLOW_INPUT_BUF_SIZE]; // Regex or text pattern
    uint8_t         priority;       // 1-100, higher = runs first
    bool            is_enabled;

    wf_step_t       steps[WORKFLOW_MAX_STEPS];
    uint8_t         step_count;

    // Runtime stats
    uint16_t        trigger_count;
    uint16_t        success_count;
    uint16_t        failure_count;
    uint32_t        last_triggered_at; // Unix timestamp
} workflow_t;

// ── Execution State ────────────────────────────────────────────────────

typedef enum {
    WF_EXEC_IDLE,
    WF_EXEC_RUNNING,
    WF_EXEC_COMPLETED,
    WF_EXEC_FAILED,
} wf_exec_status_t;

typedef struct {
    const workflow_t *workflow;
    uint8_t         current_step;
    wf_exec_status_t status;
    uint32_t        started_at;
    uint32_t        duration_ms;
    char            result_buf[WORKFLOW_RESULT_BUF_SIZE];
    uint16_t        tool_calls;
    char            error_msg[128];
} wf_exec_t;

// ── Match Result ───────────────────────────────────────────────────────

typedef struct {
    const workflow_t *workflow;
    float           match_score;  // 0.0-1.0
} wf_match_t;

// ── Public API ───────────────────────────────────────────────────────

/**
 * Initialize workflow engine.
 * Call once at startup after NVS is ready.
 */
esp_err_t workflow_engine_init(void);

/**
 * Load workflows from a JSON array.
 * Replaces all existing workflows.
 * Format: [{ "name": "...", "trigger_pattern": "...", "steps": [...] }, ...]
 */
esp_err_t workflow_engine_load(const char *json_workflows, size_t json_len);

/**
 * Add a single workflow to the engine.
 * Used for incremental sync.
 */
esp_err_t workflow_engine_add(const workflow_t *wf);

/**
 * Remove a workflow by name.
 */
esp_err_t workflow_engine_remove(const char *name);

/**
 * Find matching workflow for input text.
 * Returns the highest-priority match.
 * Returns NULL if no match.
 */
const workflow_t *workflow_engine_match(const char *input_text);

/**
 * Find ALL matching workflows, ordered by priority.
 * Writes up to max_count matches into matches[].
 * Returns number of matches found.
 */
int workflow_engine_match_all(const char *input_text,
                            wf_match_t *matches,
                            int max_count);

/**
 * Execute a workflow synchronously.
 * Blocks until all steps complete (or error).
 * On success: status = WF_EXEC_COMPLETED, result_buf contains summary
 * On failure: status = WF_EXEC_FAILED, error_msg contains reason
 */
esp_err_t workflow_engine_execute(const workflow_t *wf,
                                const char *input_text,
                                wf_exec_t *out_exec);

/**
 * Check if any workflow is registered.
 */
int workflow_engine_count(void);

/**
 * Get a workflow by name.
 */
const workflow_t *workflow_engine_get(const char *name);

/**
 * Enable/disable a workflow.
 */
esp_err_t workflow_engine_set_enabled(const char *name, bool enabled);

/**
 * Get the current execution state (for the running workflow).
 * Safe to call from another task.
 */
const wf_exec_t *workflow_engine_get_exec_state(void);

/**
 * Abort the current execution.
 */
esp_err_t workflow_engine_abort(void);

/**
 * Get memory usage estimate.
 */
void workflow_engine_mem_info(uint32_t *used, uint32_t *free);

/**
 * Build a JSON summary of all workflows (for debug/serial output).
 * out_buf must be at least 512 bytes.
 */
int workflow_engine_list_json(char *out_buf, size_t out_sz);

/**
 * Check if input matches a pattern.
 * Supports: exact match, contains match, regex (^ prefix)
 */
bool workflow_pattern_match(const char *pattern, const char *input);

#ifdef __cplusplus
}
#endif

#endif /* WORKFLOW_ENGINE_H */
