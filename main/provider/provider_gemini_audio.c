/*
 * ESPClaw - provider/provider_gemini_audio.c
 *
 * Send PCM audio to Gemini multimodal API (generateContent)
 * for speech-to-text + LLM reasoning in a single API call.
 *
 * Uses the same API key as the OpenAI-compatible provider.
 * Endpoint: generativelanguage.googleapis.com/v1beta/models/{model}:generateContent
 */
#include "provider_gemini_audio.h"
#include "provider.h"
#include "platform.h"
#include "config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "gemini_audio";

/* Model for audio requests — must support audio input */
#define GEMINI_AUDIO_MODEL "gemini-2.5-flash"

/* Reuse API key from provider_openai.c */
extern char s_api_key[];  /* defined in provider_openai.c */

/* ── WAV header helper ───────────────────────────────────────────────── */

static void write_wav_header(uint8_t *buf, uint32_t pcm_bytes,
                              uint16_t channels, uint32_t sample_rate,
                              uint16_t bits_per_sample)
{
    uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    uint16_t block_align = channels * bits_per_sample / 8;
    uint32_t data_size = pcm_bytes;
    uint32_t file_size = 36 + data_size;

    /* RIFF header */
    memcpy(buf + 0,  "RIFF", 4);
    memcpy(buf + 4,  &file_size, 4);
    memcpy(buf + 8,  "WAVE", 4);
    /* fmt sub-chunk */
    memcpy(buf + 12, "fmt ", 4);
    uint32_t fmt_size = 16;
    memcpy(buf + 16, &fmt_size, 4);
    uint16_t audio_fmt = 1; /* PCM */
    memcpy(buf + 20, &audio_fmt, 2);
    memcpy(buf + 22, &channels, 2);
    memcpy(buf + 24, &sample_rate, 4);
    memcpy(buf + 28, &byte_rate, 4);
    memcpy(buf + 32, &block_align, 2);
    memcpy(buf + 34, &bits_per_sample, 2);
    /* data sub-chunk */
    memcpy(buf + 36, "data", 4);
    memcpy(buf + 40, &data_size, 4);
}

/* ── HTTP event handler (same pattern as provider_openai) ────────────── */

typedef struct {
    char  *buf;
    size_t buf_sz;
    size_t written;
} audio_http_ctx_t;

static esp_err_t audio_http_event_handler(esp_http_client_event_t *evt)
{
    audio_http_ctx_t *ctx = (audio_http_ctx_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        size_t remaining = ctx->buf_sz - ctx->written - 1;
        size_t to_copy = ((size_t)evt->data_len < remaining)
                         ? (size_t)evt->data_len : remaining;
        if (to_copy > 0) {
            memcpy(ctx->buf + ctx->written, evt->data, to_copy);
            ctx->written += to_copy;
            ctx->buf[ctx->written] = '\0';
        }
    }
    return ESP_OK;
}

/* ── Extract text from Gemini generateContent response ───────────────── */

static void extract_gemini_text(const char *resp, char *out, size_t out_sz)
{
    /* Look for "text":"..." in the response */
    const char *text_key = "\"text\":\"";
    const char *pos = strstr(resp, text_key);
    if (!pos) {
        strncpy(out, "[error] Could not parse Gemini response", out_sz - 1);
        return;
    }
    pos += strlen(text_key);

    size_t i = 0;
    while (*pos && *pos != '"' && i < out_sz - 1) {
        if (*pos == '\\' && *(pos + 1)) {
            pos++;
            switch (*pos) {
                case 'n': out[i++] = '\n'; break;
                case 't': out[i++] = '\t'; break;
                case '"': out[i++] = '"';  break;
                case '\\': out[i++] = '\\'; break;
                default: out[i++] = *pos; break;
            }
        } else {
            out[i++] = *pos;
        }
        pos++;
    }
    out[i] = '\0';
}

/* ── Public API ──────────────────────────────────────────────────────── */

esp_err_t gemini_audio_complete(
    const int16_t *pcm_samples,
    size_t         num_samples,
    const char    *system_prompt,
    char          *response_buf,
    size_t         response_sz)
{
    if (!pcm_samples || num_samples == 0 || !response_buf || response_sz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(s_api_key) == 0) {
        ESP_LOGE(TAG, "No API key configured");
        return ESP_ERR_NOT_FOUND;
    }

    /* 1. Build WAV in PSRAM: 44-byte header + PCM data */
    size_t pcm_bytes = num_samples * sizeof(int16_t);
    size_t wav_size  = 44 + pcm_bytes;

    uint8_t *wav_buf = heap_caps_malloc(wav_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wav_buf) {
        ESP_LOGE(TAG, "OOM for WAV buffer (%u bytes)", (unsigned)wav_size);
        return ESP_ERR_NO_MEM;
    }

    write_wav_header(wav_buf, pcm_bytes, 1, 16000, 16);
    memcpy(wav_buf + 44, pcm_samples, pcm_bytes);

    ESP_LOGI(TAG, "WAV built: %u bytes (%.1f s audio)",
             (unsigned)wav_size, (float)num_samples / 16000);

    /* 2. Base64 encode WAV */
    size_t b64_len = 0;
    /* First call: get required length */
    mbedtls_base64_encode(NULL, 0, &b64_len, wav_buf, wav_size);

    char *b64_buf = heap_caps_malloc(b64_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b64_buf) {
        ESP_LOGE(TAG, "OOM for base64 buffer (%u bytes)", (unsigned)b64_len);
        heap_caps_free(wav_buf);
        return ESP_ERR_NO_MEM;
    }

    int ret = mbedtls_base64_encode((unsigned char *)b64_buf, b64_len + 1,
                                     &b64_len, wav_buf, wav_size);
    heap_caps_free(wav_buf);  /* WAV no longer needed */

    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 encode failed: %d", ret);
        heap_caps_free(b64_buf);
        return ESP_FAIL;
    }
    b64_buf[b64_len] = '\0';

    ESP_LOGI(TAG, "Base64 encoded: %u chars", (unsigned)b64_len);

    /* 3. Build JSON request body in PSRAM */
    const char *user_text = "Người dùng vừa nói qua microphone sau khi gọi wake word. "
                            "Hãy nghe audio và trả lời bằng văn bản ngắn gọn.";

    /* Estimate JSON size: fixed overhead + base64 + system prompt */
    size_t sys_len = system_prompt ? strlen(system_prompt) : 0;
    size_t json_sz = b64_len + sys_len + 2048;

    char *body = heap_caps_malloc(json_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) {
        ESP_LOGE(TAG, "OOM for JSON body (%u bytes)", (unsigned)json_sz);
        heap_caps_free(b64_buf);
        return ESP_ERR_NO_MEM;
    }

    int len = 0;

    /* Build the request */
    if (system_prompt && sys_len > 0) {
        len = snprintf(body, json_sz,
            "{\"contents\":[{\"parts\":["
            "{\"text\":\"%s\"},"
            "{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"",
            user_text);
    } else {
        len = snprintf(body, json_sz,
            "{\"contents\":[{\"parts\":["
            "{\"text\":\"%s\"},"
            "{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"",
            user_text);
    }

    /* Append base64 data */
    if (len + (int)b64_len + 512 < (int)json_sz) {
        memcpy(body + len, b64_buf, b64_len);
        len += b64_len;
    }
    heap_caps_free(b64_buf);  /* base64 no longer needed */

    /* Close inline_data and parts */
    len += snprintf(body + len, json_sz - len, "\"}}]}]");

    /* Add system instruction if provided */
    if (system_prompt && sys_len > 0) {
        /* Escape system prompt for JSON (simple: just truncate at any control chars) */
        len += snprintf(body + len, json_sz - len,
            ",\"systemInstruction\":{\"parts\":[{\"text\":\"%.*s\"}]}",
            (int)(sys_len > 2000 ? 2000 : sys_len), system_prompt);
    }

    len += snprintf(body + len, json_sz - len, "}");

    ESP_LOGI(TAG, "Request body: %d bytes", len);

    /* 4. Build URL */
    char url[256];
    snprintf(url, sizeof(url),
             "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s",
             GEMINI_AUDIO_MODEL, s_api_key);

    /* 5. Send HTTPS POST */
    if (!espclaw_tls_lock(pdMS_TO_TICKS(60000))) {
        ESP_LOGW(TAG, "TLS lock timeout");
        heap_caps_free(body);
        return ESP_ERR_TIMEOUT;
    }

    char *resp = heap_caps_malloc(LLM_RESPONSE_BUF_SIZE,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!resp) {
        espclaw_tls_unlock();
        heap_caps_free(body);
        return ESP_ERR_NO_MEM;
    }
    resp[0] = '\0';

    audio_http_ctx_t ctx = { .buf = resp, .buf_sz = LLM_RESPONSE_BUF_SIZE };

    esp_http_client_config_t cfg = {
        .url              = url,
        .method           = HTTP_METHOD_POST,
        .timeout_ms       = LLM_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler    = audio_http_event_handler,
        .user_data        = &ctx,
        .buffer_size      = 2048,
        .buffer_size_tx   = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        espclaw_tls_unlock();
        heap_caps_free(body);
        heap_caps_free(resp);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, len);

    ESP_LOGI(TAG, "Sending audio to Gemini (%s)...", GEMINI_AUDIO_MODEL);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "HTTP %d: %s", status, esp_err_to_name(err));
        ESP_LOGE(TAG, "Response: %.500s", resp);
    }

    esp_http_client_cleanup(client);
    espclaw_tls_unlock();
    heap_caps_free(body);

    if (err != ESP_OK || status != 200) {
        heap_caps_free(resp);
        return ESP_FAIL;
    }

    /* 6. Extract response text */
    extract_gemini_text(resp, response_buf, response_sz);
    ESP_LOGI(TAG, "Gemini audio response (%d chars): %.100s%s",
             (int)strlen(response_buf), response_buf,
             strlen(response_buf) > 100 ? "..." : "");

    heap_caps_free(resp);
    return ESP_OK;
}
