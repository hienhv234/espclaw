/*
 * ESPClaw - provider/provider_openai.c
 * OpenAI-compatible API (also covers OpenRouter and Ollama).
 * POST /v1/chat/completions  {"model":..., "messages":[...]}
 */
#include "provider.h"
#include "platform.h"
#include "config.h"
#include "tool/tool_registry.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "openai";

typedef struct {
    char  *buf;
    size_t buf_sz;
    size_t written;
} http_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_ctx_t *ctx = (http_ctx_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        size_t remaining = ctx->buf_sz - ctx->written - 1;
        size_t to_copy   = (evt->data_len < (int)remaining)
                           ? (size_t)evt->data_len : remaining;
        if (to_copy > 0) {
            memcpy(ctx->buf + ctx->written, evt->data, to_copy);
            ctx->written += to_copy;
            ctx->buf[ctx->written] = '\0';
        }
    }
    return ESP_OK;
}

/* Extract from: {"choices":[{"message":{"content":"..."}}]} */
static void extract_content(const char *json, char *out, size_t out_sz)
{
    /* Skip null content (tool_calls response has "content":null) */
    const char *key = strstr(json, "\"content\":\"");
    if (!key) {
        /* Check for tool_calls — caller handles this case via finish_reason */
        strncpy(out, "[no content in response]", out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }
    const char *start = key + strlen("\"content\":\"");
    size_t pos = 0;
    while (*start && pos < out_sz - 1) {
        if (start[0] == '\\' && start[1] == '"') {
            out[pos++] = '"';  start += 2;
        } else if (start[0] == '\\' && start[1] == 'n') {
            out[pos++] = '\n'; start += 2;
        } else if (start[0] == '\\' && start[1] == '\\') {
            out[pos++] = '\\'; start += 2;
        } else if (start[0] == '"') {
            break;
        } else {
            out[pos++] = *start++;
        }
    }
    out[pos] = '\0';
}

/*
 * Convert OpenAI tool_calls format to Anthropic-like format that
 * agent_loop's try_dispatch_tool() can parse.
 *
 * OpenAI response (finish_reason=tool_calls):
 *   {"choices":[{"message":{"tool_calls":[{"id":"call_xxx","function":
 *     {"name":"gpio_write","arguments":"{\"pin\":2,\"state\":1}"}}]}}]}
 *
 * We synthesize a string containing stop_reason=tool_use + id + name + input
 * so agent_loop can reuse the same parsing code.
 */
static void extract_tool_call(const char *json, char *out, size_t out_sz)
{
    /* Extract tool call id */
    char tool_id[64] = "unknown_id";
    const char *id_key = strstr(json, "\"id\":\"");
    if (id_key) {
        id_key += strlen("\"id\":\"");  /* skip "id":" and point to value start */
        size_t i = 0;
        while (*id_key && *id_key != '"' && i < sizeof(tool_id) - 1)
            tool_id[i++] = *id_key++;
        tool_id[i] = '\0';
    }

    /* Extract function name */
    char func_name[64] = "";
    const char *name_key = strstr(json, "\"name\":\"");
    if (name_key) {
        const char *start = name_key + strlen("\"name\":\"");
        size_t i = 0;
        while (*start && *start != '"' && i < sizeof(func_name) - 1)
            func_name[i++] = *start++;
        func_name[i] = '\0';
    }

    /* Extract arguments (already a JSON string, need to unescape) */
    char args[512] = "{}";
    const char *args_key = strstr(json, "\"arguments\":\"");
    if (args_key) {
        const char *start = args_key + strlen("\"arguments\":\"");
        size_t i = 0;
        while (*start && i < sizeof(args) - 1) {
            if (start[0] == '\\' && start[1] == '"') {
                args[i++] = '"'; start += 2;
            } else if (start[0] == '\\' && start[1] == '\\') {
                args[i++] = '\\'; start += 2;
            } else if (start[0] == '\\' && start[1] == 'n') {
                args[i++] = '\n'; start += 2;
            } else if (start[0] == '"') {
                break;
            } else {
                args[i++] = *start++;
            }
        }
        args[i] = '\0';
    }

    /* Extract Google thought signature if present */
    char thought_sig[128] = "";
    const char *sig_key = strstr(json, "\"thought_signature\":\"");
    if (sig_key) {
        sig_key += strlen("\"thought_signature\":\"");
        size_t i = 0;
        while (*sig_key && *sig_key != '"' && i < sizeof(thought_sig) - 1) {
            thought_sig[i++] = *sig_key++;
        }
        thought_sig[i] = '\0';
    }

    /*
     * Synthesize a string that try_dispatch_tool() can parse:
     * contains "stop_reason":"tool_use", "id", "name", "input"
     */
    snprintf(out, out_sz,
             "{\"stop_reason\":\"tool_use\","
             "\"id\":\"%s\","
             "\"name\":\"%s\","
             "\"input\":%s,"
             "\"thought_sig\":\"%s\"}",
             tool_id, func_name, args[0] ? args : "{}", thought_sig);
}

/* API key shared with provider_gemini_audio.c via extern */
char s_api_key[LLM_API_KEY_BUF_SIZE];
static char s_model[64];
static char s_base_url[128];
static bool s_bearer_auth;  /* OpenAI/OpenRouter use Bearer, Ollama has no auth */

static esp_err_t openai_init(const char *api_key, const char *model,
                             const char *base_url)
{
    strncpy(s_api_key,  api_key,  sizeof(s_api_key)  - 1);
    strncpy(s_model,    model,    sizeof(s_model)     - 1);
    strncpy(s_base_url, base_url && strlen(base_url) > 0
                        ? base_url : LLM_API_URL_OPENAI,
            sizeof(s_base_url) - 1);
    s_bearer_auth = (strlen(s_api_key) > 0);
    return ESP_OK;
}

static esp_err_t openai_complete(
    const char *system_prompt,
    const char *messages_json,
    const char *tools_json,
    char       *response_buf,
    size_t      response_sz)
{
    char *body = malloc(LLM_REQUEST_BUF_SIZE);
    if (!body) return ESP_ERR_NO_MEM;

    int len = 0;
    len += snprintf(body + len, LLM_REQUEST_BUF_SIZE - len,
        "{\"model\":\"%s\",\"max_tokens\":%d,\"messages\":[",
        s_model, LLM_MAX_TOKENS);

    if (system_prompt && strlen(system_prompt) > 0) {
        /* JSON-escape the system prompt */
        len += snprintf(body + len, LLM_REQUEST_BUF_SIZE - len,
                        "{\"role\":\"system\",\"content\":\"");
        const char *sp = system_prompt;
        while (*sp && len < LLM_REQUEST_BUF_SIZE - 2) {
            unsigned char c = (unsigned char)*sp++;
            if      (c == '"')  { body[len++] = '\\'; body[len++] = '"';  }
            else if (c == '\\') { body[len++] = '\\'; body[len++] = '\\'; }
            else if (c == '\n') { body[len++] = '\\'; body[len++] = 'n';  }
            else if (c == '\r') { body[len++] = '\\'; body[len++] = 'r';  }
            else if (c == '\t') { body[len++] = '\\'; body[len++] = 't';  }
            else                { body[len++] = (char)c; }
        }
        body[len++] = '"';
        body[len++] = '}';
        body[len++] = ',';
        body[len]   = '\0';
    }

    /* Append user messages (strip outer brackets from messages_json) */
    if (messages_json && strlen(messages_json) > 2) {
        /* messages_json is "[{...},{...}]" — copy inner content */
        const char *inner = messages_json + 1;
        size_t inner_len  = strlen(inner) - 1; /* drop trailing ] */
        if (inner_len > 0 && len + (int)inner_len < LLM_REQUEST_BUF_SIZE - 4) {
            memcpy(body + len, inner, inner_len);
            len += inner_len;
        }
    }

    /* Close messages array */
    len += snprintf(body + len, LLM_REQUEST_BUF_SIZE - len, "]");

    /* Build OpenAI-format tools JSON inline (ignoring tools_json arg which is Anthropic format) */
    char *oai_tools = malloc(LLM_REQUEST_BUF_SIZE);
    if (oai_tools) {
        int tlen = tool_registry_build_tools_json_openai(oai_tools, LLM_REQUEST_BUF_SIZE);
        if (tlen > 2) {
            len += snprintf(body + len, LLM_REQUEST_BUF_SIZE - len,
                ",\"tools\":%s", oai_tools);
        }
        free(oai_tools);
    }

    len += snprintf(body + len, LLM_REQUEST_BUF_SIZE - len, "}");

    /* Debug: log request body (first 500 chars) */
    ESP_LOGI(TAG, "Request body (%d bytes): %.500s%s", len, body,
             len > 500 ? "..." : "");

    if (!espclaw_tls_lock(pdMS_TO_TICKS(60000))) {
        ESP_LOGW(TAG, "TLS lock timeout");
        free(body);
        return ESP_ERR_TIMEOUT;
    }

    char full_url[1024];
    if (strstr(s_base_url, "/chat/completions") != NULL) {
        strncpy(full_url, s_base_url, sizeof(full_url) - 1);
    } else {
        size_t ulen = strlen(s_base_url);
        char temp_base[128];
        strncpy(temp_base, s_base_url, sizeof(temp_base) - 1);
        temp_base[sizeof(temp_base) - 1] = '\0';
        if (ulen > 0 && temp_base[ulen - 1] == '/') {
            temp_base[ulen - 1] = '\0';
        }
        snprintf(full_url, sizeof(full_url), "%s/chat/completions", temp_base);
    }
    full_url[sizeof(full_url) - 1] = '\0';

    /* If target is Google Gemini API, automatically append key as query parameter for GFE gateway bypass */
    if (strstr(full_url, "generativelanguage.googleapis.com") != NULL) {
        char temp[2048];
        snprintf(temp, sizeof(temp), "%s?key=%s", full_url, s_api_key);
        strncpy(full_url, temp, sizeof(full_url) - 1);
        full_url[sizeof(full_url) - 1] = '\0';
    }

    ESP_LOGI(TAG, "Connecting to: %s", full_url);

    char *resp = malloc(LLM_RESPONSE_BUF_SIZE);
    if (!resp) { espclaw_tls_unlock(); free(body); return ESP_ERR_NO_MEM; }

    resp[0] = '\0';
    http_ctx_t ctx = { .buf = resp, .buf_sz = LLM_RESPONSE_BUF_SIZE };

    esp_http_client_config_t cfg = {
        .url              = full_url,
        .method           = HTTP_METHOD_POST,
        .timeout_ms       = LLM_HTTP_TIMEOUT_MS,
        .crt_bundle_attach= esp_crt_bundle_attach,
        .event_handler    = http_event_handler,
        .user_data        = &ctx,
        .buffer_size      = 2048,
        .buffer_size_tx   = 2048,
    };

    esp_err_t err = ESP_FAIL;
    int status = 0;
    int max_retries = 3;
    int retry_count = 0;

    while (retry_count < max_retries) {
        resp[0] = '\0';
        ctx.buf = resp;
        ctx.buf_sz = LLM_RESPONSE_BUF_SIZE;

        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) {
            ESP_LOGE(TAG, "Failed to init HTTP client");
            break;
        }

        esp_http_client_set_header(client, "Content-Type", "application/json; charset=utf-8");
        if (s_bearer_auth) {
            char auth_val[LLM_API_KEY_BUF_SIZE + 8];
            snprintf(auth_val, sizeof(auth_val), "Bearer %s", s_api_key);
            esp_http_client_set_header(client, "Authorization", auth_val);
            esp_http_client_set_header(client, "x-goog-api-key", s_api_key);
        }

        esp_http_client_set_post_field(client, body, len);

        err = esp_http_client_perform(client);
        status = esp_http_client_get_status_code(client);

        /* Debug: log response on error */
        if (err != ESP_OK || status != 200) {
            ESP_LOGE(TAG, "HTTP %d: %s, status=%d", err, esp_err_to_name(err), status);
            ESP_LOGE(TAG, "Response: %.500s", resp);
        }

        esp_http_client_cleanup(client);

        if (err == ESP_OK && status == 429) {
            retry_count++;
            uint32_t delay_ms = 4000 * retry_count;
            ESP_LOGW(TAG, "Rate limit (429) hit. Releasing lock and waiting %d ms before retry %d/%d...", (int)delay_ms, retry_count, max_retries);
            
            espclaw_tls_unlock(); // Release lock during backoff delay to let other tasks run
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            
            if (!espclaw_tls_lock(pdMS_TO_TICKS(60000))) { // Re-acquire lock for next attempt
                ESP_LOGE(TAG, "Failed to re-acquire TLS lock");
                break;
            }
            continue;
        }

        break;
    }

    espclaw_tls_unlock();
    free(body);

    if (err != ESP_OK || status != 200) {
        free(resp);
        return ESP_FAIL;
    }

    extract_content(resp, response_buf, response_sz);

    /*
     * Step 6: detect tool_calls finish_reason (OpenAI format).
     * If found, synthesize Anthropic-like JSON so agent_loop can
     * reuse the same try_dispatch_tool() parsing code.
     */
    if (strstr(resp, "\"finish_reason\":\"tool_calls\"") ||
        strstr(resp, "\"tool_calls\":[")) {
        extract_tool_call(resp, response_buf, response_sz);
        ESP_LOGI(TAG, "tool_calls detected");
    } else {
        extract_content(resp, response_buf, response_sz);
        ESP_LOGI(TAG, "Got %d chars", (int)strlen(response_buf));
    }

    free(resp);
    return ESP_OK;
}

const provider_ops_t openai_provider = {
    .name     = "openai",
    .init     = openai_init,
    .complete = openai_complete,
    .deinit   = NULL,
};

/* OpenRouter and Ollama share the same wire format — reuse with different defaults */
const provider_ops_t openrouter_provider = {
    .name     = "openrouter",
    .init     = openai_init,
    .complete = openai_complete,
    .deinit   = NULL,
};

const provider_ops_t ollama_provider = {
    .name     = "ollama",
    .init     = openai_init,
    .complete = openai_complete,
    .deinit   = NULL,
};
