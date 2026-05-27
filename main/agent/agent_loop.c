/*
 * ESPClaw - agent/agent_loop.c
 *
 * Step 6.5: Workflow-First Agent Loop
 *
 * Changes from Step 6:
 *   - Workflow engine: try to match input against registered workflows FIRST
 *   - If workflow matches → execute → reply with result
 *   - If no workflow → fall back to LLM ReAct loop
 *   - This enables programmable skill logic without LLM for common tasks
 */
#include "agent_loop.h"
#include "session.h"
#include "context_builder.h"
#include "tool/tool_registry.h"
#include "provider/provider.h"
#include "bus/message_bus.h"
#include "messages.h"
#include "config.h"
#include "platform.h"
#include "util/json_util.h"
#include "util/ratelimit.h"
#include "workflow/workflow_engine.h"
#if ESPCLAW_HAS_WAKEWORD
#include "wakeword/wakeword.h"
#include "display_ui.h"
#include "provider/provider_gemini_audio.h"
#endif
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG       = "agent";
static message_bus_t *s_bus = NULL;

#if CONFIG_ESPCLAW_DISPLAY_OLED || CONFIG_ESPCLAW_DISPLAY_TFT
#if ESPCLAW_HAS_WAKEWORD
#define WAKE_AGENT_DONE(msg) do { \
    if ((msg).source == MSG_SOURCE_WAKE) { \
        wakeword_set_agent_busy(false); \
    } \
    if (wakeword_is_enabled()) { \
        display_ui_set_state(DISPLAY_STATE_LISTENING); \
    } else { \
        display_ui_set_state(DISPLAY_STATE_READY); \
    } \
} while (0)
#else
#define WAKE_AGENT_DONE(msg) do { \
    display_ui_set_state(DISPLAY_STATE_READY); \
} while (0)
#endif
#else
#if ESPCLAW_HAS_WAKEWORD
#define WAKE_AGENT_DONE(msg) do { \
    if ((msg).source == MSG_SOURCE_WAKE) { \
        wakeword_set_agent_busy(false); \
    } \
} while (0)
#else
#define WAKE_AGENT_DONE(msg) ((void)0)
#endif
#endif

/* Per-task state (one agent task only) */
static session_t *s_session = NULL;

/* -----------------------------------------------------------------------
 * Parse Anthropic tool_use block and dispatch to tool_registry.
 *
 * Anthropic raw response (when stop_reason=tool_use):
 *   {"content":[...,{"type":"tool_use","id":"toolu_xxx","name":"gpio_write",
 *                    "input":{"pin":2,"state":1}}],"stop_reason":"tool_use"}
 *
 * Returns true if a tool_use was detected, dispatched, and result written.
 * ----------------------------------------------------------------------- */
static bool try_dispatch_tool(const char *reply,
                               char       *tool_id_out,  size_t tool_id_sz,
                               char       *tool_name_out,size_t tool_name_sz,
                               char       *input_out,    size_t input_sz,
                               char       *thought_sig_out, size_t thought_sig_sz,
                               char       *result_buf,   size_t result_sz)
{
    if (!strstr(reply, "\"stop_reason\":\"tool_use\"")) return false;

    /* Extract tool id */
    if (!json_get_str(reply, "id", tool_id_out, tool_id_sz)) {
        strncpy(tool_id_out, "unknown_id", tool_id_sz - 1);
    }

    /* Extract tool name */
    if (!json_get_str(reply, "name", tool_name_out, tool_name_sz)) {
        snprintf(result_buf, result_sz, "Error: could not parse tool name");
        return true; /* still a tool_use, just broken */
    }

    /* Extract input object (complete JSON object with proper boundary) */
    const char *input_start = json_get_object(reply, "input");
    if (input_start) {
        /* Use json_copy_object to get the complete object with correct boundary */
        if (json_copy_object(input_start, input_out, input_sz) < 0) {
            strncpy(input_out, "{}", input_sz - 1);
        }
    } else {
        strncpy(input_out, "{}", input_sz - 1);
    }

    /* Extract Google thought signature if present in the synthesized reply */
    if (thought_sig_out && thought_sig_sz > 0) {
        if (!json_get_str(reply, "thought_sig", thought_sig_out, thought_sig_sz)) {
            thought_sig_out[0] = '\0';
        }
    }

    tool_registry_dispatch(tool_name_out, input_out, result_buf, result_sz);
    return true;
}

/* -----------------------------------------------------------------------
 * Agent task
 * ----------------------------------------------------------------------- */
static void agent_task(void *arg)
{
    inbound_msg_t  in;
    outbound_msg_t out;

    char *msgs_json   = ESPCLAW_MALLOC(LLM_REQUEST_BUF_SIZE);
    char *tools_json  = ESPCLAW_MALLOC(LLM_REQUEST_BUF_SIZE);
    char *reply       = ESPCLAW_MALLOC(LLM_RESPONSE_BUF_SIZE);
    char *sys_prompt  = ESPCLAW_MALLOC(SYSTEM_PROMPT_BUF_SIZE);
    char *tool_id     = malloc(64);
    char *tool_name   = malloc(32);
    char *tool_input  = malloc(256);
    char *thought_sig  = malloc(128);
    char *tool_result = malloc(TOOL_RESULT_BUF_SIZE);

    if (!msgs_json || !tools_json || !reply || !sys_prompt ||
        !tool_id   || !tool_name || !tool_input || !thought_sig || !tool_result) {
        ESP_LOGE(TAG, "OOM at startup");
        goto done;
    }

    if (!s_session) {
        s_session = ESPCLAW_MALLOC(sizeof(session_t));
        if (!s_session) {
            ESP_LOGE(TAG, "Failed to allocate session in PSRAM");
            goto done;
        }
    }
    session_init(s_session);
    tool_registry_init();
    workflow_engine_init();

    /* Build tools JSON once (static table, doesn't change at runtime) */
    if (tool_registry_build_tools_json(tools_json, LLM_REQUEST_BUF_SIZE) < 0) {
        ESP_LOGW(TAG, "tools JSON truncated");
        tools_json[0] = '\0';
    }

    ESP_LOGI(TAG, "ReAct agent ready (%d tools, %d workflows, history=%d, max_rounds=%d)",
             tool_registry_count(), workflow_engine_count(), MAX_HISTORY_TURNS, MAX_TOOL_ROUNDS);

    while (1) {
        if (xQueueReceive(s_bus->inbound, &in, portMAX_DELAY) != pdTRUE)
            continue;

        /* Auto-clear history if conversation is idle for > 5 minutes */
        static uint32_t last_msg_time = 0;
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
        if (last_msg_time > 0 && (now - last_msg_time > 300)) {
            session_clear(s_session);
            ESP_LOGI(TAG, "Conversation idle for >5 minutes, history cleared.");
        }
        last_msg_time = now;

        /* Support manual clear command */
        if (strcasecmp(in.text, "clear") == 0 ||
            strcasecmp(in.text, "xoa") == 0 ||
            strcmp(in.text, "xóa") == 0 ||
            strcmp(in.text, "xóa lịch sử") == 0) {
            session_clear(s_session);
            out.target = in.source;
            out.chat_id = in.chat_id;
            snprintf(out.text, sizeof(out.text), "Đã xóa lịch sử trò chuyện!");
            xQueueSend(s_bus->outbound, &out, pdMS_TO_TICKS(200));
            WAKE_AGENT_DONE(in);
            continue;
        }

#if ESPCLAW_HAS_WAKEWORD
        if (in.source == MSG_SOURCE_WAKE) {
            wakeword_set_agent_busy(true);
        }
#endif

#if CONFIG_ESPCLAW_DISPLAY_OLED || CONFIG_ESPCLAW_DISPLAY_TFT
        display_ui_set_state(DISPLAY_STATE_THINKING);
#endif

        const provider_ops_t *llm = provider_get_active();
        if (!llm) {
            snprintf(out.text, sizeof(out.text),
                     "[error] No LLM configured. Set API key via menuconfig.");
            out.target  = in.source;
            out.chat_id = in.chat_id;
            message_bus_post_outbound(s_bus, &out, pdMS_TO_TICKS(200));
            WAKE_AGENT_DONE(in);
            continue;
        }

        /* Rate limit check */
        char rl_reason[128];
        if (!ratelimit_check(rl_reason, sizeof(rl_reason))) {
            snprintf(out.text, sizeof(out.text), "[rate limited] %s", rl_reason);
            out.target  = in.source;
            out.chat_id = in.chat_id;
            message_bus_post_outbound(s_bus, &out, pdMS_TO_TICKS(200));
            if (in.audio_data) { free(in.audio_data); in.audio_data = NULL; }
            WAKE_AGENT_DONE(in);
            continue;
        }

        /* ─── VOICE AUDIO HANDLING ──────────────────────────────────
         * If inbound message carries audio data (from wake word voice
         * capture), skip the text ReAct loop and call Gemini multimodal
         * API directly for STT + LLM in one shot.
         * ─────────────────────────────────────────────────────── */
        if (in.audio_data && in.audio_samples > 0) {
            ESP_LOGI(TAG, "Voice audio: %u samples -> Gemini multimodal",
                     (unsigned)in.audio_samples);

            context_build_system_prompt(sys_prompt, SYSTEM_PROMPT_BUF_SIZE, NULL);

            esp_err_t audio_err = gemini_audio_complete(
                in.audio_data, in.audio_samples,
                sys_prompt,
                out.text, sizeof(out.text));

            /* Free audio buffer (allocated in PSRAM by voice_capture) */
            free(in.audio_data);
            in.audio_data = NULL;

            if (audio_err != ESP_OK) {
                snprintf(out.text, sizeof(out.text),
                         "[error] Voice processing failed: %s",
                         esp_err_to_name(audio_err));
            } else {
                ratelimit_record_request();
            }

            out.target  = in.source;
            out.chat_id = in.chat_id;
            message_bus_post_outbound(s_bus, &out, pdMS_TO_TICKS(200));
            WAKE_AGENT_DONE(in);
            continue;
        }

        /* ─── WORKFLOW-FIRST ROUTING ─────────────────────────────────
         * Try to match against registered workflows first.
         * If a workflow matches → execute it → return result.
         * If no match → fall through to LLM ReAct loop.
         * ─────────────────────────────────────────────────────── */
        if (workflow_engine_count() > 0) {
            const workflow_t *matched = workflow_engine_match(in.text);
            if (matched) {
                ESP_LOGI(TAG, "Workflow matched: %s (priority=%d, steps=%d)",
                         matched->name, matched->priority, matched->step_count);

                wf_exec_t exec;
                esp_err_t exec_err = workflow_engine_execute(matched, in.text, &exec);

                /* Build response from execution result */
                if (exec_err == ESP_OK && exec.status == WF_EXEC_COMPLETED) {
                    snprintf(out.text, sizeof(out.text),
                             "[WF] %s\n%s",
                             matched->name,
                             exec.result_buf);
                } else if (exec_err == ESP_OK && exec.status == WF_EXEC_FAILED) {
                    /* Workflow failed → fall through to LLM */
                    ESP_LOGW(TAG, "Workflow %s failed: %s. Falling back to LLM.",
                             matched->name, exec.error_msg);
                    /* Don't send error to user yet — fall through to LLM */
                } else {
                    snprintf(out.text, sizeof(out.text),
                             "[Workflow error] %s: %s",
                             matched->name,
                             esp_err_to_name(exec_err));
                    out.target  = in.source;
                    out.chat_id = in.chat_id;
                    message_bus_post_outbound(s_bus, &out, pdMS_TO_TICKS(200));
                    WAKE_AGENT_DONE(in);
                    continue;
                }

                out.target  = in.source;
                out.chat_id = in.chat_id;
                message_bus_post_outbound(s_bus, &out, pdMS_TO_TICKS(200));
                WAKE_AGENT_DONE(in);
                continue;
            }
        }

        /* 1. Add user turn */
        session_append(s_session, "user", in.text);

        /* 2. Build system prompt */
        context_build_system_prompt(sys_prompt, SYSTEM_PROMPT_BUF_SIZE, NULL);

        /* 3. ReAct loop */
        bool got_reply = false;
        for (int round = 0; round < MAX_TOOL_ROUNDS; round++) {

            /* Choose message format based on provider type */
            bool use_openai_format = (llm->name &&
                (strcmp(llm->name, "openai") == 0 ||
                 strcmp(llm->name, "openrouter") == 0 ||
                 strcmp(llm->name, "ollama") == 0 ||
                 strcmp(llm->name, "custom") == 0));

            int jlen = use_openai_format
                ? session_build_messages_json_openai(s_session, msgs_json, LLM_REQUEST_BUF_SIZE)
                : session_build_messages_json(s_session, msgs_json, LLM_REQUEST_BUF_SIZE);
            if (jlen < 0) ESP_LOGW(TAG, "messages JSON truncated");

            ESP_LOGI(TAG, "-> LLM round %d (%d chars)", round,
                     jlen > 0 ? jlen : 0);

            esp_err_t err = llm->complete(sys_prompt, msgs_json, tools_json,
                                          reply, LLM_RESPONSE_BUF_SIZE);
            if (err == ESP_OK) {
                ratelimit_record_request();
            }
            if (err != ESP_OK) {
                /* Clear history on call failure to heal state from any stale or corrupted signatures */
                session_clear(s_session);
                snprintf(out.text, sizeof(out.text),
                         "[error] LLM call failed: %s", esp_err_to_name(err));
                got_reply = true;
                break;
            }

            /* 4. Tool dispatch? */
            tool_id[0] = tool_name[0] = tool_input[0] = tool_result[0] = thought_sig[0] = '\0';
            if (try_dispatch_tool(reply,
                                   tool_id,    64,
                                   tool_name,  32,
                                   tool_input, 256,
                                   thought_sig, 128,
                                   tool_result, TOOL_RESULT_BUF_SIZE)) {
                session_append_tool_use(s_session, tool_id, tool_name, tool_input, thought_sig);
                session_append_tool_result(s_session, tool_id, tool_result);
                ESP_LOGI(TAG, "Tool: %s(%s) [sig_len=%d] -> %s", tool_name, tool_input, (int)strlen(thought_sig), tool_result);
                continue;
            }

            /* 5. Plain text reply — done */
            session_append(s_session, "assistant", reply);
            strncpy(out.text, reply, sizeof(out.text) - 1);
            out.text[sizeof(out.text) - 1] = '\0';
            got_reply = true;
            break;
        }

        if (!got_reply) {
            snprintf(out.text, sizeof(out.text),
                     "[error] Max tool rounds (%d) exceeded.", MAX_TOOL_ROUNDS);
        }

        out.target  = in.source;
        out.chat_id = in.chat_id;
        message_bus_post_outbound(s_bus, &out, pdMS_TO_TICKS(200));

        WAKE_AGENT_DONE(in);
    }

done:
    ESPCLAW_FREE(msgs_json);
    ESPCLAW_FREE(tools_json);
    ESPCLAW_FREE(reply);
    ESPCLAW_FREE(sys_prompt);
    free(tool_id);
    free(tool_name);
    free(tool_input);
    free(thought_sig);
    free(tool_result);
    vTaskDelete(NULL);
}

esp_err_t agent_start(message_bus_t *bus)
{
    s_bus = bus;

    BaseType_t ret = ESPCLAW_CREATE_PINNED(
        "agent", agent_task, AGENT_TASK_STACK_SIZE,
        NULL, AGENT_TASK_PRIORITY, NULL, ESPCLAW_CORE_AGENT);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create agent task");
        return ESP_FAIL;
    }
    return ESP_OK;
}
