# ESPClaw — Tài liệu Kỹ thuật Toàn diện

> **Phiên bản:** 1.0  
> **Ngày:** 29/04/2026  
> **Ngôn ngữ gốc firmware:** ESP-IDF C (Không có Lua script — xem Mục 7.3)  
> **Frontend:** Next.js + react-force-graph-3D  
> **Backend:** Supabase (PostgreSQL) + Next.js API Routes  
> **Phần cứng:** ESP32-S3 DevKit / ESP32-C3

---

## Mục lục

1. [Tổng quan Kiến trúc Hệ thống](#1-tổng-quan-kiến-trúc-hệ-thống)
2. [ESP32 Firmware — ESP-IDF C Chi tiết](#2-esp32-firmware--esp-idf-c-chi-tiết)
3. [Backend & Database](#3-backend--database)
4. [3D Knowledge Graph — react-force-graph-3D](#4-3d-knowledge-graph--react-force-graph-3d)
5. [Flow Tổng thể (Backend + ESP)](#5-flow-tổng-thể-backend--esp)
6. [Đề xuất Cơ chế Node, Task Definition & Lua Script](#6-đề-xuất-cơ-chế-node-task-definition--lua-script)
7. [Ví dụ Code Mẫu — 3D Graph Nâng cao](#7-ví-dụ-code-mẫu--3d-graph-nâng-cao)
8. [Bảng Tra Cứu Nhanh](#8-bảng-tra-cứu-nhanh)

---

## 1. Tổng quan Kiến trúc Hệ thống

### 1.1 Sơ đồ Kiến trúc Tổng thể

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              USER LAYER                                      │
│   ┌─────────────────┐    ┌──────────────────┐    ┌────────────────────┐  │
│   │  Telegram Bot    │    │  3D Knowledge    │    │   SSH / Serial     │  │
│   │  (text/message)  │    │  Graph Dashboard  │    │   CLI Console      │  │
│   └────────┬────────┘    └────────┬─────────┘    └─────────┬──────────┘  │
│            │                        │                        │             │
└────────────┼────────────────────────┼────────────────────────┼─────────────┘
             │                        │                        │
             ▼                        ▼                        ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           TRANSPORT / API LAYER                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                    Next.js API Routes (Edge)                          │   │
│  │   /api/devices/otp  ·  /api/devices/pair  ·  /api/devices/status     │   │
│  │   /api/neuron/sync  ·  /api/neuron/graph  ·  /api/neuron/pulse        │   │
│  └──────────────────────────────────┬───────────────────────────────────┘   │
│                                     │ Supabase REST                         │
│  ┌──────────────────────────────────▼───────────────────────────────────┐   │
│  │                    Supabase PostgreSQL                               │   │
│  │  devices · nodes · links · pulses · graph_stats · personas          │   │
│  │  RLS Policies · Row Level Security · Functions · Triggers           │   │
│  └──────────────────────────────────┬───────────────────────────────────┘   │
│                                     │                                        │
│  ┌──────────────────────────────────▼───────────────────────────────────┐   │
│  │              NeuronLink MQTT Broker (HiveMQ Cloud TLS)               │   │
│  │     espclaw/{device_id}/cmd  ·  espclaw/{device_id}/otp             │   │
│  │     espclaw/{device_id}/response  ·  espclaw/{device_id}/status     │   │
│  └──────────────────────────────────┬───────────────────────────────────┘   │
│                                     │ MQTT over TLS 8883                    │
└─────────────────────────────────────┼───────────────────────────────────────┘
                                      │
┌─────────────────────────────────────┼───────────────────────────────────────┐
│                             ESP32 FIRMWARE LAYER                             │
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                    REACT AGENT LOOP (FreeRTOS)                        │   │
│  │                                                                       │   │
│  │  inbound_queue ──► [1] Rate Limit  [2] Session Append              │   │
│  │                      │                       │                      │   │
│  │                      ▼                       ▼                      │   │
│  │                 [3] System Prompt  ──► [4] LLM API Call              │   │
│  │                      Build               (OpenAI/Anthropic)          │   │
│  │                                              │                       │   │
│  │                                              ▼                       │   │
│  │                                    [5] Tool Dispatch?                │   │
│  │                                       YES: Execute tool              │   │
│  │                                       NO:  → outbound_queue          │   │
│  │                                                                       │   │
│  └──────────────────────────────────┬───────────────────────────────────┘   │
│                                     │                                        │
│  ┌──────────────────────────────────▼───────────────────────────────────┐   │
│  │                    TOOL REGISTRY (20 tools)                           │   │
│  │  gpio_write · gpio_read · wifi_scan · memory_set · memory_get       │   │
│  │  cron_schedule · cron_cancel · set_persona · get_diagnostics ...    │   │
│  └──────────────────────────────────┬───────────────────────────────────┘   │
│                                     │                                        │
│  ┌──────────────────────────────────▼───────────────────────────────────┐   │
│  │                    10 CHANNEL OUTPUT TASKS                            │   │
│  │                                                                       │   │
│  │  Serial ──► Telegram ──► MQTT ──► DingTalk ──► Discord               │   │
│  │  Slack ──► WeCom ──► Lark ──► PushPlus ──► Bark                     │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                    HARDWARE / SENSOR LAYER                            │   │
│  │  WiFi STA/AP · GPIO · USB Serial JTAG · OLED · I2S Audio            │   │
│  │  (Không có driver BME280 / BH1750 — chỉ có HAL GPIO)                 │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 1.2 Luồng Dữ liệu Chính

```
User gửi tin nhắn Telegram
        │
        ▼
Telegram Bot API (long polling)
        │
        ▼
ESP32 nhận update qua HTTPS
        │
        ▼
message_bus_post_inbound(text, TELEGRAM, chat_id)
        │
        ▼
AGENT TASK (Core 1, FreeRTOS)
        │
        ├── Rate limit check (hourly/daily)
        ├── session_append("user", text)
        ├── context_build_system_prompt()
        │
        ├── LLM API Call (OpenAI/Anthropic)
        │       │
        │       ├── SYSTEM: System prompt + 20 tools
        │       ├── USER: Conversation history
        │       └── ASSISTANT: LLM response
        │
        ├── LLM trả tool_use → tool_registry_dispatch()
        │       │
        │       ├── gpio_write → hal_gpio_write()
        │       ├── cron_schedule → cron_service_add()
        │       ├── memory_set → nvs_manager_set()
        │       └── wifi_scan → esp_wifi_scan_start()
        │
        └── LLM trả text thường
                │
                ▼
        message_bus_post_outbound(text, TELEGRAM, chat_id)
                │
                ▼
        Telegram output task → HTTPS POST sendMessage
                │
                ▼
        User nhận phản hồi trên Telegram
```

### 1.3 Các thành phần chính

| Thành phần | Công nghệ | Dòng code | Ngôn ngữ |
|---|---|---|---|
| ESP32 Firmware | ESP-IDF v5.x | ~8,000 | C |
| Web Dashboard | Next.js 14 | ~3,500 | TypeScript/TSX |
| Database | Supabase PostgreSQL | Schema | SQL |
| MQTT Broker | HiveMQ Cloud | — | — |
| 3D Graph | react-force-graph-3D | — | TypeScript |
| OTA Updates | GitHub Releases | — | — |
| LLM Providers | OpenAI / Anthropic | — | REST API |

### 1.4 Kiến trúc Bộ nhớ (ESP32-S3 vs ESP32-C3)

```
ESP32-S3 (PSRAM enabled)
├── LLM Request buffer:  32KB (PSRAM)
├── LLM Response buffer: 32KB (PSRAM)
├── Session history:     32KB (PSRAM)
├── Tool result buffer:  1KB  (SRAM)
├── Agent stack:         12KB (SRAM)
└── Channel stacks:      8KB each (SRAM)

ESP32-C3 (No PSRAM)
├── LLM Request buffer:  8KB  (SRAM)
├── LLM Response buffer: 8KB  (SRAM)
├── Session history:     DISABLED
├── Tool result buffer:  512B (SRAM)
├── Agent stack:         8KB (SRAM)
└── Channel stacks:      4KB each (SRAM)
```

---

## 2. ESP32 Firmware — ESP-IDF C Chi tiết

### 2.1 Chuỗi Khởi động (`main/main.c`)

```c:40:100:main/main.c
// Thứ tự khởi tạo (không có Lua script)
void app_main(void) {
    // 1. Tạo device ID từ MAC address
    generate_device_id();  // WiFi STA MAC → hash → base-36 → "GETAI-XXXXX"

    // 2. OLED init (non-fatal)
    oled_init();  // I2C GPIO46/SDA, GPIO45/SCL

    // 3. NVS init
    nvs_flash_init();

    // 4. Utility init
    init_tls_lock();           // Mutex cho HTTPS (ngăn OOM khi gọi song song)
    ratelimit_init();           // Load hourly/daily counters từ NVS
    persona_init();             // Load persona mặc định

    // 5. Cron service init (NTP sync)
    cron_service_init();

    // 6. WiFi init
    wifi_mgr_init_and_connect();  // NVS → Kconfig fallback

    // 7. LLM provider init
    provider_registry_init();     // Gọi init của OpenAI + Anthropic provider

    // 8. GPIO HAL init
    hal_gpio_init();              // Thiết lập GPIO an toàn

    // 9. NeuronLink init (Supabase sync)
    neuron_link_init();           // Sync cache với cloud

    // 10. Message bus init (2 FreeRTOS queues)
    message_bus_init(&s_bus);    // inbound(8) + outbound(8) queues

    // 11. Agent start (pinned Core 1 trên S3)
    agent_start(&s_bus);

    // 12. Cron start
    cron_service_start(&s_bus);

    // 13. Channel registry → start all available channels
    channel_registry_init(&s_bus);

    // 14. HTTP server (config portal)
    http_server_start();
}
```

### 2.2 Agent Loop — Thuật toán ReAct

```c:35:180:main/agent/agent_loop.c
// Mã giả chi tiết của agent_task (FreeRTOS task, Core 1)
static void agent_task(void *arg) {
    message_bus_t *bus = (message_bus_t *)arg;
    inbound_msg_t msg;
    char reply[MAX_LLM_REPLY_LEN];

    while (true) {
        // Bước 1: Đợi tin nhắn đến (blocking)
        if (xQueueReceive(bus->inbound, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // Bước 2: Rate limit check
        if (!ratelimit_check()) {
            snprintf(reply, sizeof(reply),
                "⚠️ Rate limit exceeded. Please wait.\n"
                "Hourly: %d/%d | Daily: %d/%d",
                ratelimit_get_hourly(), RATELIMIT_MAX_PER_HOUR,
                ratelimit_get_daily(), RATELIMIT_MAX_PER_DAY);
            message_bus_post_outbound(bus, reply, msg.source, msg.chat_id);
            continue;
        }

        // Bước 3: Thêm vào lịch sử hội thoại (ring buffer)
        session_append(&s_session, "user", msg.text);

        // Bước 4: Xây dựng system prompt
        char sys_prompt[MAX_SYS_PROMPT_LEN];
        context_build_system_prompt(sys_prompt, sizeof(sys_prompt));

        // Bước 5: Vòng lặp ReAct — gọi LLM nhiều lần nếu cần tool
        for (int round = 0; round < MAX_TOOL_ROUNDS; round++) {
            // Bước 5a: Build messages JSON (Anthropic hoặc OpenAI format)
            char msgs_json[MAX_LLM_JSON_LEN];
            int msg_len = session_build_messages_json(&s_session, msgs_json,
                sizeof(msgs_json), LLM_FMT_ANTHROPIC);

            // Bước 5b: Build tools JSON (20 tools)
            char tools_json[MAX_TOOLS_JSON_LEN];
            int tools_len = tool_registry_build_tools_json(tools_json,
                sizeof(tools_json));

            // Bước 5c: Gọi LLM API (TLS mutex lock bên trong)
            esp_err_t err = s_llm->complete(sys_prompt, msgs_json,
                tools_len > 0 ? tools_json : NULL, reply, sizeof(reply));

            if (err != ESP_OK) {
                snprintf(reply, sizeof(reply),
                    "❌ LLM error: %s", esp_err_to_name(err));
                session_pop_last(&s_session);  // Rollback user message
                break;
            }

            // Bước 5d: Parse LLM response
            // Try dispatch tool nếu LLM trả tool_use
            tool_result_t tool_result;
            bool did_tool = try_dispatch_tool(reply, &tool_result);

            if (did_tool) {
                // Có tool_use → lưu vào session và tiếp tục vòng lặp
                session_append_tool_use(&s_session,
                    tool_result.id, tool_result.name, tool_result.input_json);
                session_append_tool_result(&s_session,
                    tool_result.id, tool_result.result);
                continue;  // ← QUAN TRỌNG: tiếp tục gọi LLM lần nữa
            }

            // Không có tool_use → đây là phản hồi cuối cùng
            session_append(&s_session, "assistant", reply);
            message_bus_post_outbound(bus, reply, msg.source, msg.chat_id);
            break;  // Thoát ReAct loop
        }
    }
}
```

### 2.3 Session — Ring Buffer cho Lịch sử Hội thoại

```c:20:80:main/agent/session.c
// Ring buffer lưu 24 (S3) hoặc 8 (C3) lượt hội thoại
typedef struct {
    conversation_msg_t msgs[MAX_HISTORY_TURNS];  // Ring buffer
    int head;      // Vị trí tin nhắn cũ nhất
    int count;     // Số tin nhắn hiện tại (0 - MAX_HISTORY_TURNS)
    int16_t pending_tool_use_count;  // Đếm số tool_use chưa có result
} session_t;

// Cấu trúc tin nhắn
typedef struct {
    char role[16];           // "user", "assistant", "system"
    char content[1024];      // Nội dung (hoặc JSON cho tool_use)
} conversation_msg_t;

// Khi ring buffer đầy → ghi đè tin nhắn cũ nhất
// JSON escaping: " → \" , \ → \\ , \n → \\n , \r → \\r , \t → \\t , 0x00-0x1F → \uXXXX

// Anthropic format (mặc định):
// {
//   "role": "assistant",
//   "content": [
//     {"type": "text", "text": "..."},
//     {"type": "tool_use", "id": "toolu_xxx", "name": "gpio_write",
//      "input": {"pin": 48, "state": 1}}
//   ]
// }
```

### 2.4 Tool Registry — 20 Công cụ tích hợp

```c:10:60:main/tool/tool_registry.c
// Mẫu X-Macro đăng ký tool
#define TOOL_ENTRY(tname, tdesc, tschema, tfn) \
    { .name=(tname), .description=(tdesc), \
      .input_schema_json=(tschema), .execute=(tfn) },

static const tool_def_t s_tools[] = {
#include "builtin_tools.def"
};

// Đăng ký 20 tools từ builtin_tools.def
// Dispatch tool:
bool tool_registry_dispatch(const char *tool_name, const char *input_json,
                            char *result_buf, size_t result_buf_size) {
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, tool_name) == 0) {
            return s_tools[i].execute(input_json, result_buf, result_buf_size);
        }
    }
    return false;
}
```

### 2.5 Chi tiết 20 Tools

| # | Tên Tool | Handler | Input Schema | Output |
|---|---|---|---|---|
| 1 | `gpio_write` | `tool_gpio_write` | `{pin: int, state: int}` | `"OK: GPIO{pin}={state}"` |
| 2 | `gpio_read` | `tool_gpio_read` | `{pin: int}` | `"GPIO{pin}={level}"` |
| 3 | `gpio_read_all` | `tool_gpio_read_all` | `{}` | JSON array các GPIO |
| 4 | `delay` | `tool_delay` | `{milliseconds: int}` | `"Delayed {ms}ms"` |
| 5 | `memory_set` | `tool_memory_set` | `{key: str, value: str}` | `"Saved"` (key phải `u_*`) |
| 6 | `memory_get` | `tool_memory_get` | `{key: str}` | Giá trị hoặc `"null"` |
| 7 | `memory_delete` | `tool_memory_delete` | `{key: str}` | `"Deleted"` |
| 8 | `memory_list` | `tool_memory_list` | `{}` | JSON array các key `u_*` |
| 9 | `get_diagnostics` | `tool_system_diagnostics` | `{}` | heap, uptime, GPIO pins |
| 10 | `get_version` | `tool_get_version` | `{}` | Firmware version |
| 11 | `cron_schedule` | `tool_cron_schedule` | `{type, action, interval_s?, hour?, minute?, delay_s?}` | Task ID |
| 12 | `cron_list` | `tool_cron_list` | `{}` | JSON array các task đã đặt |
| 13 | `cron_cancel` | `tool_cron_cancel` | `{id: int}` | `"Cancelled {id}"` |
| 14 | `cron_cancel_all` | `tool_cron_cancel_all` | `{}` | `"All cancelled"` |
| 15 | `get_time` | `tool_get_time` | `{}` | ISO8601 timestamp |
| 16 | `set_timezone` | `tool_set_timezone` | `{timezone: str}` | `"Timezone set"` |
| 17 | `set_persona` | `set_persona_exec` | `{persona: str}` | `"Persona: {name}"` |
| 18 | `get_persona` | `get_persona_exec` | `{}` | Tên persona hiện tại |
| 19 | `wifi_scan` | `tool_wifi_scan` | `{}` | JSON array các AP gần |
| 20 | `get_network_info` | `tool_get_network_info` | `{}` | SSID, RSSI, IP, gateway |

### 2.6 GPIO HAL — Safety Guardrails

```c:15:60:main/tool/tool_gpio.c
// Bảo mật GPIO qua 3 lớp:

// Lớp 1: Kconfig range (biên dịch)
#ifdef CONFIG_ESPCLAW_GPIO_MIN_PIN
    #define GPIO_MIN_PIN  CONFIG_ESPCLAW_GPIO_MIN_PIN
#endif

// Lớp 2: Runtime check trong handler
esp_err_t tool_gpio_write(const char *input, char *out, size_t out_sz) {
    int pin, state;
    parse_json(input, &pin, &state);

    if (pin < GPIO_MIN_PIN || pin > GPIO_MAX_PIN) {
        snprintf(out, out_sz, "ERR: Pin %d out of allowed range [%d-%d]",
            pin, GPIO_MIN_PIN, GPIO_MAX_PIN);
        return ESP_FAIL;
    }

    // Lớp 3: CSV allowlist (nếu được cấu hình)
    if (!gpio_is_allowed(pin)) {
        snprintf(out, out_sz, "ERR: Pin %d not in allowlist", pin);
        return ESP_FAIL;
    }

    return hal_gpio_write(pin, state);
}
```

### 2.7 Cron Service

```c:30:100:main/service/cron_service.c
// Cron service hoạt động độc lập với agent
// Mỗi task cron = một chuỗi text gửi vào inbound queue

typedef enum {
    CRON_TYPE_PERIODIC,  // Lặp lại mỗi X giây (min 10s)
    CRON_TYPE_DAILY,      // Mỗi ngày vào giờ cố định
    CRON_TYPE_ONCE       // Chạy một lần sau delay_s
} cron_type_t;

// Lưu trong NVS namespace "ec_cron"
// Mỗi task: {id, type, action, next_run_utc, interval_s, enabled}

// Ví dụ: "Mỗi ngày 8h sáng báo thời tiết"
cron_schedule("daily", action="Thời tiết hôm nay thế nào?",
    hour=8, minute=0)
```

### 2.8 WiFi Manager — Kết nối & AP Portal

```c:20:100:main/net/wifi_manager.c
// Luồng kết nối WiFi:
esp_err_t wifi_mgr_init_and_connect(void) {
    // 1. Đọc SSID/password từ NVS
    nvs_get_str("wifi_ssid") → nvs_get_str("wifi_pass")

    // 2. Nếu không có → dùng Kconfig fallback
    //    CONFIG_ESPCLAW_WIFI_SSID / CONFIG_ESPCLAW_WIFI_PASS

    // 3. esp_wifi_set_mode(WIFI_MODE_STA)
    // 4. esp_wifi_set_config() với credentials
    // 5. esp_wifi_start()

    // 6. Chờ GOT_IP event (timeout 30s)
    //    → EventGroupWaitBits(WIFI_CONNECTED_BIT | WIFI_GOT_IP_BIT)

    // 7. Nếu timeout → tự động khởi động AP portal
}

// AP Portal (wifi_ap.c):
// - AP: "ESP-XXXXXXXX" (8 ký tự cuối của device ID)
// - Password: "12345678"
// - DNS: always redirect sang 192.168.4.1
// - HTTP server: /connect  (POST: ssid, pass)
//               /status    (GET: ssid, ip, rssi)
//               /scan      (GET: danh sách AP)
```

### 2.9 Neuron Link — Supabase Sync Protocol

```c:20:80:main/net/neuron_link.c
// Đồng bộ 2 chiều ESP ↔ Supabase

typedef enum {
    SYNC_MODE_DELTA,   // Chỉ thay đổi từ lần sync cuối
    SYNC_MODE_FULL,     // Toàn bộ graph
    SYNC_MODE_FORCE     // Bỏ qua last_sync, force full
} neuron_sync_mode_t;

// Sync flow:
neuron_link_sync(SYNC_MODE_DELTA) {
    // 1. GET /rest/v1/nodes?updated_at=gt.{last_sync}&tenant_id=eq.{tid}
    // 2. GET /rest/v1/links?updated_at=gt.{last_sync}&tenant_id=eq.{tid}
    // 3. Parse response → ghi vào NVS cache
    // 4. Cập nhật last_sync timestamp trong NVS

    // Giới hạn cache (NVS):
    //   Max 100 nodes, 200 links, 500 pulses
}

// Pulse (xung nơ-ron — link được kích hoạt):
// Khi một action xảy ra → tạo pulse record:
// { source_node_id, target_node_id, type, energy, text, device_id }
// → POST /rest/v1/pulses
// → Link tương ứng có last_pulsed_at → hiệu ứng animation trên 3D graph
```

### 2.10 Tất cả 10 Channels — Chi tiết

```
┌────────────────────────────────────────────────────────────────────┐
│                    CHANNELS — Kiến trúc Chung                       │
│                                                                     │
│  Mỗi channel đều có:                                               │
│  1. Config từ NVS (ưu tiên) hoặc Kconfig (fallback)               │
│  2. Output FreeRTOS queue riêng                                    │
│  3. Output task riêng (blocked trên queue)                         │
│  4. .post(text) function để gửi tin nhắn                          │
│  5. Conditional compilation: #ifdef CONFIG_ESPCLAW_CHANNEL_X      │
└────────────────────────────────────────────────────────────────────┘

SERIAL ──────────────────────────────────────────────────────────────
  Protocol:   USB Serial JTAG (VFS driver)
  Input:      serial_input_task → line-by-line → inbound queue
  Output:     serial_output_task → printf("%s\n")
  Local cmds: /help /tools /heap /gpio /reset /weblocal
  UART pins:  USB Native (không dùng GPIO rx/tx)

TELEGRAM ─────────────────────────────────────────────────────────────
  Protocol:   Telegram Bot API via getUpdates (long polling)
  Polling:    timeout=5s, limit=1 per request
  Security:   Token + Chat ID whitelist
  Persistence: Offset stored in NVS (prevent replay on reboot)
  Backoff:    5s → 10s → 20s → 40s → 80s → 160s → 5min (max)
  TLS:        esp_mbedtls (global mutex lock)

MQTT ─────────────────────────────────────────────────────────────────
  Broker:     e855d1adcb91498097194e25175017dd.s1.eu.hivemq.cloud:8883
  Protocol:   MQTT over TLS + ALPN "mqtt"
  Topics:     espclaw/{device_id}/cmd          (subscribe)
              espclaw/{device_id}/otp          (subscribe)
              espclaw/{device_id}/login_success (subscribe)
              espclaw/{device_id}/response     (publish)
              espclaw/{device_id}/status       (publish LWT)
  Client ID:  esp32-{random_hex}
  Credentials: Hardcoded trong source (không Kconfig)

DINGTALK ─────────────────────────────────────────────────────────────
  Protocol:   HTTPS POST webhook
  Format:     {"msgtype":"text","text":{"content":"..."}}
  Security:   HMAC-SHA256 signature in URL query params

DISCORD ──────────────────────────────────────────────────────────────
  Protocol:   HTTPS POST webhook
  Format:     {"content":"..."}

SLACK ─────────────────────────────────────────────────────────────────
  Protocol:   HTTPS POST webhook
  Format:     {"text":"..."}

WECOM ─────────────────────────────────────────────────────────────────
  Protocol:   HTTPS POST webhook
  Format:     {"msgtype":"markdown","markdown":{"content":"..."}}

LARK ──────────────────────────────────────────────────────────────────
  Protocol:   HTTPS POST webhook + signature header
  Format:     {"msg_type":"text","content":{"text":"..."}}
  Security:   HMAC-SHA256(timestamp + "\n" + secret) → Base64

PUSHPULS ──────────────────────────────────────────────────────────────
  Protocol:   HTTP POST
  Format:     {"token":"...","title":"ESPClaw","content":"..."}

BARK ──────────────────────────────────────────────────────────────────
  Protocol:   HTTP GET (URL percent-encoded)
  URL:        api.day.app/{key}/ESPClaw/{url_encoded_text}
  Charset:    UTF-8 percent encoding
```

### 2.11 FreeRTOS Task Architecture

```
Task Name             │ Priority │ Stack │ Core  │ Mục đích
──────────────────────┼──────────┼───────┼───────┼──────────────────────
agent_task            │    5     │ 12KB  │  Core1│ ReAct agent loop
serial_input_task     │    5     │  8KB  │  Core0│ Đọc serial → inbound
serial_output_task    │    5     │  8KB  │  Core0│ Serial out ← outbound
tg_poll_task          │    5     │ 12KB  │  Core0│ Telegram long polling
tg_output_task        │    5     │ 12KB  │  Core0│ Telegram send queue
cron_task             │    4     │  8KB  │  Core0│ NTP sync + scheduled
mqtt_publish_task     │    5     │  8KB  │  Core0│ MQTT outbound queue
[X]_output_task       │    5     │  8KB  │  Core0│ Each notification ch
wifi_reconnect_task   │    3     │  4KB  │  Core0│ WiFi auto-reconnect
```

### 2.12 LLM Provider — Dual Format Support

```c:20:80:main/provider/provider_openai.c
// OpenAI-compatible provider (OpenAI, OpenRouter, Ollama)
// Hỗ trợ BOTH Anthropic và OpenAI message format

esp_err_t openai_complete(const char *sys_prompt, const char *msgs_json,
                           const char *tools_json, char *out, size_t out_sz) {
    // 1. Build HTTP request body (OpenAI tool_calls format)
    //    {
    //      "model": "...",
    //      "messages": [...],
    //      "tools": [...],        // if tools_json != NULL
    //      "tool_choice": "auto"
    //    }

    // 2. HTTPS POST với TLS mutex lock

    // 3. Parse response:
    //    - stop_reason: "tool_calls" → extract tool_calls[].function
    //    - stop_reason: "stop" → extract content

    // 4. Nếu provider trả OpenAI tool_calls format:
    //    → Chuyển đổi thành Anthropic tool_use format string
    //    → trả về cho agent_loop để reuse cùng try_dispatch_tool()

    // 5. Lưu usage stats vào NVS (prompt_tokens, completion_tokens)
}
```

---

## 3. Backend & Database

### 3.1 Supabase Schema

```sql
-- devices: Thiết bị ESP32 đã đăng ký
CREATE TABLE devices (
  id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id),
  device_id       TEXT UNIQUE NOT NULL,      -- "GETAI-XXXXX"
  display_name    TEXT,
  model           TEXT,                       -- ESP32-S3, ESP32-C3, etc.
  esp32_mac       TEXT,                       -- MAC address
  firmware_version TEXT,
  current_persona TEXT DEFAULT 'default',
  last_seen_at    TIMESTAMPTZ,
  created_at      TIMESTAMPTZ DEFAULT NOW(),
  is_online       BOOLEAN DEFAULT FALSE,
  metadata        JSONB DEFAULT '{}'
);
CREATE INDEX idx_devices_tenant ON devices(tenant_id);
CREATE INDEX idx_devices_device_id ON devices(device_id);

-- nodes: Nút kiến thức trong knowledge graph
CREATE TABLE nodes (
  id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id),
  device_id       TEXT,                       -- Nguồn tạo (nullable)
  type            TEXT NOT NULL,              -- user, device, skill, memory...
  subtype         TEXT,                       -- finance_account, health_exercise...
  name            TEXT NOT NULL,
  description     TEXT,
  pos_x           FLOAT DEFAULT 50,            -- Tọa độ 3D (0-100)
  pos_y           FLOAT DEFAULT 50,
  pos_z           FLOAT DEFAULT 50,
  is_active       BOOLEAN DEFAULT TRUE,
  is_pinned       BOOLEAN DEFAULT FALSE,
  is_favorite     BOOLEAN DEFAULT FALSE,
  content         JSONB DEFAULT '{}',          -- Dữ liệu tùy ý
  activation_count INT DEFAULT 0,             -- Số lần được kích hoạt
  pulse_strength  FLOAT DEFAULT 0,           -- 0.0 - 1.0
  link_count      INT DEFAULT 0,              -- Số link đi ra
  created_at      TIMESTAMPTZ DEFAULT NOW(),
  updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_nodes_tenant ON nodes(tenant_id);
CREATE INDEX idx_nodes_type ON nodes(type);
CREATE INDEX idx_nodes_updated ON nodes(updated_at);

-- links: Kết nối giữa các nút
CREATE TABLE links (
  id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id),
  device_id       TEXT,
  source_node_id   UUID REFERENCES nodes(id) ON DELETE CASCADE,
  target_node_id   UUID REFERENCES nodes(id) ON DELETE CASCADE,
  type            TEXT NOT NULL,              -- owns, part_of, related_to...
  weight          FLOAT DEFAULT 0.5,         -- 0.0 - 1.0
  confidence      FLOAT DEFAULT 1.0,
  is_active       BOOLEAN DEFAULT TRUE,
  is_ai_generated BOOLEAN DEFAULT FALSE,
  last_pulsed_at  TIMESTAMPTZ,
  created_at      TIMESTAMPTZ DEFAULT NOW(),
  updated_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_links_tenant ON links(tenant_id);
CREATE INDEX idx_links_source ON links(source_node_id);
CREATE INDEX idx_links_target ON links(target_node_id);
CREATE UNIQUE INDEX idx_links_unique ON links(tenant_id, source_node_id, target_node_id);

-- pulses: Xung nơ-ron — kích hoạt link
CREATE TABLE pulses (
  id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id),
  device_id       TEXT NOT NULL,
  source_node_id  UUID REFERENCES nodes(id) ON DELETE CASCADE,
  target_node_id  UUID REFERENCES nodes(id) ON DELETE CASCADE,
  type            TEXT NOT NULL,              -- interaction, automation, insight...
  energy          FLOAT DEFAULT 0.5,          -- 0.0 - 1.0
  text            TEXT,
  created_at      TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_pulses_tenant ON pulses(tenant_id);
CREATE INDEX idx_pulses_device ON pulses(device_id);
CREATE INDEX idx_pulses_created ON pulses(created_at DESC);

-- personas: Hồ sơ nhân cách AI
CREATE TABLE personas (
  id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id),
  name            TEXT NOT NULL,              -- "assistant", "coder", "analyst"
  display_name    TEXT,
  system_prompt   TEXT,
  config          JSONB DEFAULT '{}',
  created_at      TIMESTAMPTZ DEFAULT NOW(),
  updated_at      TIMESTAMPTZ DEFAULT NOW()
);

-- graph_stats: Thống kê graph
CREATE TABLE graph_stats (
  tenant_id       UUID PRIMARY KEY REFERENCES tenants(id),
  total_nodes     INT DEFAULT 0,
  total_links     INT DEFAULT 0,
  total_pulses    INT DEFAULT 0,
  last_sync_at    TIMESTAMPTZ,
  updated_at      TIMESTAMPTZ DEFAULT NOW()
);

-- RLS Policies (bảo mật dữ liệu multi-tenant)
ALTER TABLE devices ENABLE ROW LEVEL SECURITY;
ALTER TABLE nodes ENABLE ROW LEVEL SECURITY;
ALTER TABLE links ENABLE ROW LEVEL SECURITY;
ALTER TABLE pulses ENABLE ROW LEVEL SECURITY;
ALTER TABLE personas ENABLE ROW LEVEL SECURITY;

-- Mỗi table chỉ user thuộc tenant mới đọc/ghi được dữ liệu tenant đó
CREATE POLICY "Tenant isolation for devices" ON devices
  FOR ALL USING (tenant_id = current_setting('app.tenant_id')::UUID);

CREATE POLICY "Tenant isolation for nodes" ON nodes
  FOR ALL USING (tenant_id = current_setting('app.tenant_id')::UUID);

-- Triggers: Tự động cập nhật updated_at
CREATE TRIGGER update_nodes_updated_at
  BEFORE UPDATE ON nodes FOR EACH ROW EXECUTE FUNCTION
  moddatetime('updated_at');

CREATE TRIGGER update_links_updated_at
  BEFORE UPDATE ON links FOR EACH ROW EXECUTE FUNCTION
  moddatetime('updated_at');

-- Trigger: Cập nhật link_count và pulse_strength khi có pulse mới
CREATE OR REPLACE FUNCTION update_node_on_pulse()
RETURNS TRIGGER AS $$
BEGIN
  UPDATE nodes SET
    activation_count = activation_count + 1,
    pulse_strength = LEAST(1.0, pulse_strength + NEW.energy * 0.1)
  WHERE id = NEW.target_node_id;

  UPDATE links SET
    last_pulsed_at = NOW()
  WHERE source_node_id = NEW.source_node_id
    AND target_node_id = NEW.target_node_id;

  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER on_pulse_created
  AFTER INSERT ON pulses FOR EACH ROW EXECUTE FUNCTION
  update_node_on_pulse();

-- Trigger: Cập nhật graph_stats
CREATE OR REPLACE FUNCTION update_graph_stats()
RETURNS TRIGGER AS $$
BEGIN
  INSERT INTO graph_stats (tenant_id, total_nodes, total_links, total_pulses, updated_at)
  VALUES (
    NEW.tenant_id,
    (SELECT COUNT(*) FROM nodes WHERE tenant_id = NEW.tenant_id AND is_active = TRUE),
    (SELECT COUNT(*) FROM links WHERE tenant_id = NEW.tenant_id AND is_active = TRUE),
    (SELECT COUNT(*) FROM pulses WHERE tenant_id = NEW.tenant_id),
    NOW()
  )
  ON CONFLICT (tenant_id) DO UPDATE SET
    total_nodes = EXCLUDED.total_nodes,
    total_links = EXCLUDED.total_links,
    total_pulses = EXCLUDED.total_pulses,
    updated_at = NOW();
  RETURN NEW;
END;
```

### 3.2 API Routes

```
POST /api/devices/otp
  Body: { device_id: string }
  → Tạo OTP 6 chữ số, lưu Redis (5 phút TTL)
  → Gửi MQTT espclaw/{device_id}/otp
  → Response: { success: true, expires_in: 300 }

POST /api/devices/pair
  Body: { device_id: string, otp: string, tenant_id: string }
  → Verify OTP → tạo Supabase device record
  → Subscribe MQTT topics
  → Response: { success: true, device: {...} }

GET  /api/devices/status
  → Query: ?device_id=xxx
  → Kiểm tra Supabase device record
  → Response: { online: bool, last_seen: timestamp, ... }

POST /api/neuron/sync
  → POST { tenant_id, device_id, nodes[], links[] }
  → Upsert vào Supabase
  → Trả về delta changes từ cloud
  → Trigger: pulse animation nếu có links mới

GET  /api/neuron/graph
  → Query: ?tenant_id=xxx
  → Lấy toàn bộ nodes + links + stats
  → Response: { nodes: GraphNode[], links: GraphLink[], stats: {...} }

POST /api/neuron/pulse
  → POST { tenant_id, source_node_id, target_node_id, type, energy, text }
  → Tạo pulse record → trigger PostgreSQL
  → Trigger cập nhật link.last_pulsed_at
  → Broadcast real-time qua Supabase Realtime
```

### 3.3 NeuronLink Client (`lib/neuron-sync.ts`)

```typescript:lib/neuron-sync.ts
export class NeuronLinkClient {
  private supabase: SupabaseClient;
  private mqttClient: MqttClient;
  private tenantId: string;
  private deviceId: string;
  private lastSyncAt: Date | null = null;

  // === Gửi nodes/links từ ESP lên cloud ===
  async pushDelta(newNodes: GraphNode[], updatedLinks: GraphLink[]): Promise<void> {
    const { error } = await this.supabase.from('nodes').upsert(newNodes, {
      onConflict: 'id',
      ignoreDuplicates: false  // update if exists
    });

    if (updatedLinks.length > 0) {
      await this.supabase.from('links').upsert(updatedLinks, {
        onConflict: 'id'
      });
    }

    // Broadcast real-time update
    this.mqttClient.publish(`neuron/${this.tenantId}/update`, {
      nodes: newNodes,
      links: updatedLinks,
      timestamp: new Date().toISOString()
    });
  }

  // === Kéo changes từ cloud về ESP ===
  async pullDelta(): Promise<{ nodes: GraphNode[], links: GraphLink[] }> {
    // Supabase Realtime subscription (preferred)
    const channel = this.supabase
      .channel('neuron-updates')
      .on('postgres_changes', {
        event: '*',
        schema: 'public',
        table: 'nodes',
        filter: `tenant_id=eq.${this.tenantId}`
      }, payload => {
        this.onNodeChange(payload);  // Process in real-time
      })
      .subscribe();

    // Fallback: polling-based delta fetch
    if (this.lastSyncAt) {
      const { data } = await this.supabase
        .from('nodes')
        .select('*')
        .eq('tenant_id', this.tenantId)
        .gte('updated_at', this.lastSyncAt.toISOString());

      return { nodes: data || [], links: [] };
    }
    return { nodes: [], links: [] };
  }

  // === Tạo xung nơ-ron (pulse) ===
  async pulse(params: {
    sourceNodeId: string;
    targetNodeId: string;
    type: string;
    energy: number;
    text?: string;
  }): Promise<void> {
    const pulse = {
      id: crypto.randomUUID(),
      tenant_id: this.tenantId,
      device_id: this.deviceId,
      ...params,
      created_at: new Date().toISOString()
    };

    await this.supabase.from('pulses').insert(pulse);

    // Realtime: thông báo cho web dashboard
    this.mqttClient.publish(`neuron/${this.tenantId}/pulse`, pulse);
  }
}
```

### 3.4 Real-time Updates — Supabase Realtime

```typescript
// Kênh Realtime cho 3D graph dashboard
const setupGraphRealtime = (tenantId: string, onUpdate: (payload) => void) => {
  const channel = supabase
    .channel(`graph-${tenantId}`)
    .on('postgres_changes', {
      event: 'INSERT',
      schema: 'public',
      table: 'pulses',
      filter: `tenant_id=eq.${tenantId}`
    }, payload => {
      // 1. Cập nhật node pulse_strength
      // 2. Set link.last_pulsed_at → animated=true
      // 3. Trigger particle overlay redraw
      onUpdate(payload.new);
    })
    .on('postgres_changes', {
      event: '*',
      schema: 'public',
      table: 'nodes',
      filter: `tenant_id=eq.${tenantId}`
    }, payload => {
      // Thêm node mới hoặc cập nhật node
      onUpdate({ type: 'node', ...payload.new });
    })
    .on('postgres_changes', {
      event: '*',
      schema: 'public',
      table: 'links',
      filter: `tenant_id=eq.${tenantId}`
    }, payload => {
      onUpdate({ type: 'link', ...payload.new });
    })
    .subscribe();

  return () => supabase.removeChannel(channel);
};
```

---

## 4. 3D Knowledge Graph — react-force-graph-3D

### 4.1 Stack Thư viện

```
react-force-graph-3D (react wrapper, v1.29.1)
    └── 3d-force-graph (core Kapsule, v1.80.0)
         ├── three-forcegraph (Three.js Object3D + d3-force physics)
         │    └── d3-force-3d (3D force simulation)
         └── three-render-objects (scene, camera, renderer, controls)
              └── three.js (WebGL rendering engine)
```

### 4.2 Data Models

```typescript:lib/graph.ts
// Nút đồ thị — mỗi nút = một thực thể trong hệ thống
export interface GraphNode {
  id: string;
  name: string;
  type: string;           // user | device | skill | memory | tag | event | goal | pulse | ...
  subtype?: string;       // finance_account | health_exercise | coding_task | ...

  // Tọa độ 3D (0-100 trong DB, scale ×10 khi render)
  pos_x: number;
  pos_y: number;
  pos_z: number;

  // Trạng thái
  is_active: boolean;
  is_pinned: boolean;     // Cố định vị trí, không bị force simulation di chuyển
  is_favorite: boolean;
  is_focused?: boolean;   // Đang được focus (camera zoom vào)

  // Visual
  color?: string;        // Override màu mặc định
  size?: number;         // Override kích thước mặc định
  icon?: string;         // Emoji hoặc icon name
  opacity?: number;

  // Stats
  activation_count: number;  // Số lần được kích hoạt
  pulse_strength: number;    // 0.0-1.0, ảnh hưởng glow + size
  link_count: number;        // Số kết nối đi ra

  // Content
  content: Record<string, any>;  // Dữ liệu tùy ý (telegram_id, email, git repo...)

  // Timestamps
  created_at: string;
  updated_at: string;
}

// Liên kết đồ thị
export interface GraphLink {
  id: string;
  source: string;   // Node ID nguồn
  target: string;   // Node ID đích
  type: string;     // owns | part_of | related_to | caused_by | triggered | ...

  // Properties
  weight: number;       // 0.0-1.0, ảnh hưởng opacity + width
  confidence: number;   // 0.0-1.0, độ tin cậy của link
  is_active: boolean;
  is_ai_generated: boolean;

  // Visual
  color?: string;
  opacity?: number;
  width?: number;
  animated?: boolean;   // === QUAN TRỌNG: Bật particle flow animation ===
  curve_strength?: number; // Độ cong của Bezier curve (0 = thẳng)

  // Pulse state
  last_pulsed_at?: string;  // Auto-set animated=true nếu < 60 giây
}
```

### 4.3 Màu sắc Node theo Domain

```typescript:lib/graph.ts
export const DOMAIN_COLORS: Record<string, string> = {
  // Core types
  user:       '#455A64',   // Xám đậm — người dùng
  device:     '#78909C',   // Xám nhạt — thiết bị ESP
  skill:      '#2196F3',   // Xanh dương — kỹ năng
  memory:     '#9E9E9E',   // Xám trung — bộ nhớ
  tag:        '#FF9800',   // Cam — tag/phân loại
  entity:     '#7C4DFF',   // Tím — thực thể
  event:      '#E91E63',   // Hồng — sự kiện
  goal:       '#F44336',   // Đỏ — mục tiêu
  routine:    '#00BCD4',   // Cyan — thói quen
  insight:    '#AB47BC',   // Tím nhạt — insight/ý tưởng
  pulse:      '#AA00FF',   // Tím laser — xung nơ-ron
  webhook:    '#26A69A',   // Teal — webhook
  session:    '#5C6BC0',   // Indigo — phiên làm việc
  context:    '#EC407A',   // Hồng đậm — ngữ cảnh

  // Subtypes (finance)
  finance_account:     '#4CAF50',
  finance_transaction: '#8BC34A',
  finance_budget:     '#CDDC39',

  // Subtypes (health)
  health_exercise:    '#F44336',
  health_meal:        '#FF5722',
  health_sleep:       '#3F51B5',
  health_weight:      '#009688',

  // Subtypes (coding)
  coding_task:        '#2196F3',
  coding_repo:        '#673AB7',
  coding_issue:       '#E91E63',
  coding_pr:          '#FF9800',

  // Subtypes (automation)
  automation_trigger:  '#FF5722',
  automation_action:  '#4CAF50',
  automation_rule:    '#00BCD4',

  // Subtypes (social)
  telegram_chat:      '#0088CC',
  discord_server:     '#5865F2',
  email_thread:       '#EA4335',
};

export const LINK_COLORS: Record<string, string> = {
  owns:         '#4CAF50',  // Xanh — sở hữu
  part_of:      '#90A4AE',  // Xám nhạt — là một phần của
  related_to:   '#78909C',  // Xám — liên quan
  caused_by:    '#FF5722',  // Cam đậm — nguyên nhân
  triggered:    '#FF9800',  // Cam — được kích hoạt
  costs:        '#F44336',  // Đỏ — chi phí
  depends_on:   '#9C27B0',  // Tím — phụ thuộc
  blocks:       '#B71C1C',  // Đỏ đậm — chặn
  follows:     '#3F51B5',  // Indigo — theo sau
  implements:   '#009688', // Teal — triển khai
};
```

### 4.4 Kích thước Node — Công thức

```typescript:lib/graph.ts
export function getNodeSize(node: GraphNode): number {
  const base = 5;                                    // Kích thước cơ bản
  const activationBonus = Math.log10(node.activation_count + 1) * 2;  // log scale
  const pulseBonus = node.pulse_strength * 5;       // Bonus từ pulse
  return Math.min(30, base + activationBonus + pulseBonus);  // Max 30
}
// Ví dụ:
//   activation_count=0 → size=5
//   activation_count=10 → size≈7.6
//   activation_count=100 → size≈9.6
//   activation_count=1000 → size≈11.6
//   pulse_strength=1.0 → size+=5
```

### 4.5 Node tùy chỉnh — THREE.Sprite với Glow Effect

```typescript:components/KnowledgeGraph3D.tsx
const nodeThreeObject = useCallback((node: any) => {
  const size = getNodeSize(node);
  const color = getNodeColor(node);
  const isHighlighted = node.id === highlightedNodeId;

  // === Sprite với AdditiveBlending cho hiệu ứng phát sáng tự nhiên ===
  const sprite = new THREE.Sprite(
    new THREE.SpriteMaterial({
      color: color,
      transparent: true,
      opacity: 0.8 + (node.pulse_strength || 0) * 0.2,
      blending: THREE.AdditiveBlending,  // ← Glow effect
      depthWrite: false                  // Không bị occlusion
    })
  );
  sprite.scale.set(size * 2, size * 2, 1);

  // === Vòng tròn trắng khi được chọn ===
  if (isHighlighted) {
    const ringGeo = new THREE.RingGeometry(size * 1.5, size * 2, 32);
    const ringMat = new THREE.MeshBasicMaterial({
      color: '#FFFFFF',
      transparent: true,
      opacity: 0.8,
      side: THREE.DoubleSide
    });
    const ringMesh = new THREE.Mesh(ringGeo, ringMat);
    ringMesh.rotation.x = Math.PI / 2;  // Nằm ngang
    sprite.add(ringMesh);              // Child của sprite
  }

  return sprite;
}, [highlightedNodeId]);
```

### 4.6 Link tùy chỉnh — QuadraticBezierCurve3 (Đường cong)

```typescript:components/KnowledgeGraph3D.tsx
const linkThreeObject = useCallback((link: any) => {
  const source = link.source;
  const target = link.target;
  if (!source || !target || typeof source === 'string') return null;

  // === Tính control point cho đường cong Bezier ===
  const midX = (source.x + target.x) / 2;
  const midY = (source.y + target.y) / 2;
  const midZ = (source.z + target.z) / 2;

  const dx = target.x - source.x;
  const dy = target.y - source.y;
  const dz = target.z - source.z;
  const curveOffset = link.curve_strength || 0;

  const curve = new THREE.QuadraticBezierCurve3(
    new THREE.Vector3(source.x, source.y, source.z),
    new THREE.Vector3(
      midX + dz * curveOffset,
      midY + (-dx * curveOffset),
      midZ + dy * curveOffset
    ),
    new THREE.Vector3(target.x, target.y, target.z)
  );

  // === Tạo line geometry từ 50 điểm trên curve ===
  const points = curve.getPoints(50);
  const geometry = new THREE.BufferGeometry().setFromPoints(points);

  return new THREE.Line(
    geometry,
    new THREE.LineBasicMaterial({
      color: getLinkColor(link),
      transparent: true,
      opacity: getLinkOpacity(link),
      linewidth: getLinkWidth(link)
    })
  );
}, []);
```

### 4.7 Particle Overlay — Animation dòng chảy trên Link

```typescript:components/KnowledgeGraph3D.tsx
// Canvas 2D overlay để animate particles
// Vì 3d-force-graph không hỗ trợ tốt particle trên custom link objects,
// ta dùng một canvas riêng để vẽ particles

function ParticleOverlay({ particles, dimensions }: ParticleOverlayProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    let animId: number;
    const animate = () => {
      ctx.clearRect(0, 0, dimensions.width, dimensions.height);

      particles.forEach(p => {
        // Tính vị trí trên Bezier curve tại thời điểm t
        const t = p.progress;
        const t1 = 1 - t;
        const cp = p.controlPoint;

        const x = t1 * t1 * p.sourceScreen.x
               + 2 * t1 * t * cp.x
               + t * t * p.targetScreen.x;
        const y = t1 * t1 * p.sourceScreen.y
               + 2 * t1 * t * cp.y
               + t * t * p.targetScreen.y;

        // Vẽ glow particle
        const gradient = ctx.createRadialGradient(x, y, 0, x, y, 8);
        gradient.addColorStop(0, p.color);
        gradient.addColorStop(1, 'transparent');

        ctx.globalAlpha = 1 - Math.abs(t - 0.5) * 2;  // Fade ở đầu/cuối
        ctx.fillStyle = gradient;
        ctx.beginPath();
        ctx.arc(x, y, 8, 0, Math.PI * 2);
        ctx.fill();

        // Cập nhật progress
        p.progress += p.speed;
        if (p.progress > 1) p.progress = 0;
      });

      animId = requestAnimationFrame(animate);
    };

    animate();
    return () => cancelAnimationFrame(animId);
  }, [particles, dimensions]);

  return (
    <canvas
      ref={canvasRef}
      width={dimensions.width}
      height={dimensions.height}
      style={{ position: 'absolute', top: 0, left: 0, pointerEvents: 'none' }}
    />
  );
}
```

### 4.8 Camera Focus — Zoom vào Node

```typescript:components/KnowledgeGraph3D.tsx
// Khi node được click hoặc pulse, camera tự động di chuyển đến node đó
useEffect(() => {
  if (highlightedNodeId && graphRef.current) {
    const node = graphData.nodes.find(n => n.id === highlightedNodeId);
    if (node) {
      // Di chuyển camera đến vị trí node (1000ms animation)
      graphRef.current.centerAt(node.x, node.y, 1000);
      // Zoom lên mức 2x
      graphRef.current.zoom(2, 1000);
    }
  }
}, [highlightedNodeId]);

// Zoom to fit tất cả nodes
const handleZoomToFit = () => {
  graphRef.current?.zoomToFit(1000, 100);  // 1s, padding 100px
};

// Lấy tọa độ 2D từ 3D (cho particle overlay)
const handleGetScreenPos = (nodeId: string) => {
  const pos3D = graphRef.current?.graph2ScreenCoords(node.x, node.y, node.z);
  return { x: pos3D.x, y: pos3D.y };
};
```

---

## 4B. Kiến trúc claw_modules & claw_capabilities

### 4B.1 Tổng quan hệ thống plugin/capability

ESPClaw sử dụng kiến trúc **modular plugin system** gồm 2 lớp:

```
┌──────────────────────────────────────────────────────────────┐
│  LỚP 1: claw_capabilities (18 plugins)                     │
│  Mỗi plugin = 1 capability group (cap_*)                   │
│  Đăng ký tools cho LLM gọi                                  │
├──────────────────────────────────────────────────────────────┤
│  LỚP 2: claw_modules (5 core modules)                      │
│  claw_core   → ReAct agent loop, LLM integration            │
│  claw_cap    → Capability registry & tool exposure           │
│  claw_skill  → Skill loader, session skill state            │
│  claw_memory → Session history, long-term memory, profiles   │
│  claw_event_router → Event routing rules, action execution  │
└──────────────────────────────────────────────────────────────┘
```

### 4B.2 claw_modules — 5 Core Modules

#### claw_core (`claw_modules/claw_core/`)
**Core ReAct Agent Engine** — trái tim của firmware.

```c
// claw_core.h - Core API
esp_err_t claw_core_init(const claw_core_config_t *config);
esp_err_t claw_core_start(void);
esp_err_t claw_core_add_context_provider(const claw_core_context_provider_t *provider);
esp_err_t claw_core_submit(const claw_core_request_t *request, uint32_t timeout_ms);
esp_err_t claw_core_cancel_request(uint32_t request_id);
esp_err_t claw_core_receive(claw_core_response_t *response, uint32_t timeout_ms);
```

**Kiến trúc hoạt động:**
- **Request queue** (4 slots): Tiếp nhận request từ event router
- **Tool loop**: Gọi capability → parse response → gọi tiếp (max 10 vòng)
- **Response queue** (4 slots): Trả kết quả về cho router
- **LLM backends**: OpenAI-compatible (GPT-4o-mini), Anthropic (Claude 3.5 Haiku), Custom
- **Context providers**: System prompt, session history, memory, tools — được inject vào LLM
- **Completion observers**: Callbacks sau khi request hoàn thành (dùng cho memory logging)

```c
// claw_core_config_t key fields
typedef struct {
    const char *api_key;                    // LLM API key
    const char *provider;                   // "openai" | "anthropic" | "custom"
    const char *model;                     // "gpt-4o-mini" | "claude-3-5-haiku"
    const char *system_prompt;              // Base system prompt
    claw_core_call_cap_fn call_cap;        // Bridge: LLM → capability system
    uint32_t max_tool_iterations;          // Default: 10
    uint32_t request_queue_len;            // Default: 4
} claw_core_config_t;
```

**LLM HTTP Transport**: `claw_llm_http_transport.c` — xử lý authentication, retries, streaming, media pipeline.

#### claw_cap (`claw_modules/claw_cap/`)
**Capability Registry** — registry trung tâm cho tất cả capability groups.

```c
// claw_cap.h - Core API
esp_err_t claw_cap_init(void);
esp_err_t claw_cap_register(const claw_cap_descriptor_t *descriptor);
esp_err_t claw_cap_register_group(const claw_cap_group_t *group);
esp_err_t claw_cap_start_all(void);
esp_err_t claw_cap_call(const char *id_or_name, const char *input_json, ...);
char *claw_cap_build_llm_tools_json(const claw_cap_call_context_t *ctx, bool wrap_for_responses_api);
```

**Capability Kind:**
- `CLAW_CAP_KIND_CALLABLE` — Chỉ expose như tool cho LLM
- `CLAW_CAP_KIND_EVENT_SOURCE` — Chỉ phát sinh event
- `CLAW_CAP_KIND_HYBRID` — Cả hai (hầu hết đều hybrid)

**Capability Flags:**
- `CLAW_CAP_FLAG_CALLABLE_BY_LLM` — LLM được gọi trực tiếp
- `CLAW_CAP_FLAG_EMITS_EVENTS` — Phát sinh event ra router
- `CLAW_CAP_FLAG_SUPPORTS_LIFECYCLE` — Có init/start/stop hooks
- `CLAW_CAP_FLAG_RESTRICTED` — Chỉ system mới gọi được

**LLM Tool Exposure:** `claw_cap_build_llm_tools_json()` render tất cả capability có `CLAW_CAP_FLAG_CALLABLE_BY_LLM` thành JSON tool definitions (OpenAI function calling format hoặc Anthropic tool use format).

#### claw_skill (`claw_modules/claw_skill/`)
**Skill Management System** — quản lý skill document và session-scope skill activation.

```c
// claw_skill.h - Core API
esp_err_t claw_skill_init(const claw_skill_config_t *config);
esp_err_t claw_skill_reload_registry(void);

// Session-scoped skill activation
esp_err_t claw_skill_activate_for_session(const char *session_id, const char *skill_id);
esp_err_t claw_skill_deactivate_for_session(const char *session_id, const char *skill_id);
esp_err_t claw_skill_clear_active_for_session(const char *session_id);
esp_err_t claw_skill_load_active_cap_groups(const char *session_id, char ***out_group_ids, ...);

// Prompt providers
extern const claw_core_context_provider_t claw_skill_skills_list_provider;   // Full catalog
extern const claw_core_context_provider_t claw_skill_active_skill_docs_provider; // Active docs
```

**Skill = markdown document** mô tả tool cho LLM. Mỗi skill chứa:
- Khi nào dùng skill đó
- Cách gọi tools
- Input/output schema
- Ví dụ

**Session skill activation** cho phép LLM chỉ thấy subset tools phù hợp với conversation context. Ví dụ: trong Telegram chat → chỉ thấy Telegram + Scheduler skills.

#### claw_memory (`claw_modules/claw_memory/`)
**Memory System** — 4 context providers cho LLM context injection.

```c
// claw_memory.h - Core API
esp_err_t claw_memory_init(const claw_memory_config_t *config);
esp_err_t claw_memory_register_group(void);
esp_err_t claw_memory_store(const claw_memory_item_t *item);
esp_err_t claw_memory_recall(const claw_memory_query_t *query, char **out_json);
esp_err_t claw_memory_forget(const char *memory_id);

// 4 context providers
extern const claw_core_context_provider_t claw_memory_profile_provider;         // User profile
extern const claw_core_context_provider_t claw_memory_long_term_provider;       // LT memory (full)
extern const claw_core_context_provider_t claw_memory_long_term_lightweight_provider; // LT memory (summary)
extern const claw_core_context_provider_t claw_memory_session_history_provider; // Current session
```

**Memory Item Schema:**
```c
typedef struct {
    char id[40];              // UUID-like
    char source[16];          // "user" | "assistant" | "system" | "auto_extract"
    char content[256];         // Raw content
    uint16_t summary_ids[3];  // Up to 3 summary references
    char tags[96];            // Comma-separated tags
    char keywords[128];
    uint32_t created_at;
    uint32_t updated_at;
    uint16_t access_count;
    uint8_t deleted;           // Soft delete
} claw_memory_item_t;
```

**Auto-extract stage note:** Khi agent hoàn thành tool loop, `claw_memory` tự động extract key facts từ conversation vào memory (LLM-as-judge pattern).

#### claw_event_router (`claw_modules/claw_event_router/`)
**Event Routing Engine** — nhận event từ capability → match rules → execute actions.

```c
// claw_event_router.h - Core API
esp_err_t claw_event_router_init(const claw_event_router_config_t *config);
esp_err_t claw_event_router_start(void);
esp_err_t claw_event_router_handle_event(const claw_event_t *event, claw_event_router_result_t *out_result);
esp_err_t claw_event_router_add_rule_json(const char *rule_json);
esp_err_t claw_event_router_update_rule_json(const char *rule_json);
esp_err_t claw_event_router_delete_rule(const char *id);
```

**Event routing rule:**
```c
typedef struct {
    bool enabled;
    char id[64];
    char description[160];
    claw_event_router_match_t match;    // event_type, source_cap, channel, text...
    claw_event_router_action_t *actions; // call_cap, run_agent, run_script, send_message...
    size_t action_count;
} claw_event_router_rule_t;
```

**Action types:**
- `CLAW_EVENT_ROUTER_ACTION_CALL_CAP` — Gọi capability trực tiếp
- `CLAW_EVENT_ROUTER_ACTION_RUN_AGENT` — Submit request cho LLM agent
- `CLAW_EVENT_ROUTER_ACTION_RUN_SCRIPT` — Chạy Lua script
- `CLAW_EVENT_ROUTER_ACTION_SEND_MESSAGE` — Gửi message qua channel
- `CLAW_EVENT_ROUTER_ACTION_EMIT_EVENT` — Phát sinh event mới
- `CLAW_EVENT_ROUTER_ACTION_DROP` — Drop event

**Template variables:** Action inputs hỗ trợ `{{event.event_type}}`, `{{event.text}}`, `{{event.payload.kind}}` — được render trước khi execute.

**Session policies:** `chat`, `trigger`, `global`, `ephemeral`, `nosave` — kiểm soát cách event tạo session context.

### 4B.3 claw_capabilities — 18 Plugin Capability Groups

#### cap_lua — Lua Script Execution (đã mô tả ở Section 6)

#### cap_scheduler — Scheduler / Timer (11 tools)

Event-driven scheduler: chỉ phát sinh event tại thời điểm định sẵn, không tự thực thi action. Actions được định nghĩa ở `cap_router_mgr`.

```c
// cap_scheduler - 11 descriptors
scheduler_list          // List all entries
scheduler_get          // Get one by id
scheduler_add          // Add from schedule_json string
scheduler_update       // Update from schedule_json string
scheduler_remove       // Remove by id
scheduler_enable       // Enable by id
scheduler_disable      // Disable by id
scheduler_pause         // Pause by id
scheduler_resume       // Resume by id
scheduler_trigger_now  // Fire immediately
scheduler_reload       // Reload from disk
```

**Schedule kinds:**
- `once` — One-shot tại Unix epoch timestamp
- `interval` — Relative periodic (ms)
- `cron` — Wall-clock aligned (5-field cron: minute hour mday month wday)

**Payload at fire time:**
```json
{
  "schedule_id": "drink_reminder",
  "planned_time_ms": 1746000000000,
  "fire_time_ms": 1746000001000,
  "kind": "cron",
  "run_count": 5,
  "user_payload": { "message": "time to drink water" }
}
```

#### cap_router_mgr — Router Rule Management (6 tools)

CRUD cho event router rules.

```c
// cap_router_mgr - 6 descriptors
list_router_rules       // List all rules as JSON
get_router_rule         // Get one by id
add_router_rule         // Add from rule_json string
update_router_rule      // Update from rule_json string
delete_router_rule      // Delete by id
reload_router_rules     // Reload from disk
```

**典型 router rule pattern:**
```json
{
  "id": "im_any_message_agent",
  "enabled": true,
  "match": {
    "event_type": "message",
    "content_type": "text"
  },
  "actions": [
    {
      "type": "run_agent",
      "input": {
        "target_channel": "{{event.source_channel}}",
        "session_policy": "chat"
      }
    }
  ]
}
```

#### cap_session_mgr — Session Management

```c
esp_err_t cap_session_mgr_register_group(void);
esp_err_t cap_session_mgr_set_session_root_dir(const char *session_root_dir);
size_t cap_session_mgr_build_session_id(const claw_event_t *event, char *buf, ...);
```

Tự động build session ID từ event context (chat_id + sender_id). Đăng ký group `cap_session_mgr` để event router có thể resolve session per event.

#### cap_llm_inspect — Image Inspection (1 tool)

```c
inspect_image  // Analyze local image with prompt
// Input: { "path": "/storage/inbox/photo.jpg", "prompt": "Describe objects..." }
```

Gọi LLM vision API để phân tích ảnh local trên device.

#### cap_im_tg — Telegram Messaging (3 tools)

```c
tg_send_message  // Plain text
tg_send_image    // Local image file (jpg, png, gif, webp)
tg_send_file     // Generic file (txt, json, log, csv, pdf)
```

Reply trong Telegram chat hiện tại hoặc gửi file đến chat cụ thể.

#### cap_im_feishu — Feishu/Lark Messaging
#### cap_im_wechat — WeChat Messaging
#### cap_im_attachment — Attachment Handling
#### cap_im_mcp_client / cap_im_mcp_server — MCP Protocol Integration

#### cap_boards — Board / Hardware Profile Management

```python
# tools/generate_board_skill.py - Auto-generate skill từ board config
# Sinh markdown skill document chứa:
# - Board display parameters (width, height, if_type)
# - Pin mapping
# - I2C/SPI configurations
# Để LLM biết cách interact với hardware cụ thể
```

### 4B.4 Tool Taxonomy toàn hệ thống

| # | Tool Name | Group | Kind | Mô tả |
|---|-----------|-------|------|--------|
| 1 | `lua_list_scripts` | cap_lua | HYBRID | List Lua scripts |
| 2 | `lua_write_script` | cap_lua | HYBRID | Write script to storage |
| 3 | `lua_run_script` | cap_lua | HYBRID | Run sync (default 60s) |
| 4 | `lua_run_script_async` | cap_lua | HYBRID | Run async, return job_id |
| 5 | `lua_list_async_jobs` | cap_lua | HYBRID | List async jobs |
| 6 | `lua_get_async_job` | cap_lua | HYBRID | Get job details |
| 7 | `lua_stop_async_job` | cap_lua | HYBRID | Cooperative stop |
| 8 | `lua_stop_all_async_jobs` | cap_lua | HYBRID | Stop all |
| 9 | `scheduler_list` | cap_scheduler | HYBRID | List schedules |
| 10 | `scheduler_get` | cap_scheduler | HYBRID | Get one schedule |
| 11 | `scheduler_add` | cap_scheduler | HYBRID | Add schedule |
| 12 | `scheduler_update` | cap_scheduler | HYBRID | Update schedule |
| 13 | `scheduler_remove` | cap_scheduler | HYBRID | Remove schedule |
| 14 | `scheduler_enable` | cap_scheduler | HYBRID | Enable |
| 15 | `scheduler_disable` | cap_scheduler | HYBRID | Disable |
| 16 | `scheduler_pause` | cap_scheduler | HYBRID | Pause |
| 17 | `scheduler_resume` | cap_scheduler | HYBRID | Resume |
| 18 | `scheduler_trigger_now` | cap_scheduler | HYBRID | Fire now |
| 19 | `scheduler_reload` | cap_scheduler | HYBRID | Reload from disk |
| 20 | `list_router_rules` | cap_router_mgr | CALLABLE | List rules |
| 21 | `get_router_rule` | cap_router_mgr | CALLABLE | Get one rule |
| 22 | `add_router_rule` | cap_router_mgr | CALLABLE | Add rule |
| 23 | `update_router_rule` | cap_router_mgr | CALLABLE | Update rule |
| 24 | `delete_router_rule` | cap_router_mgr | CALLABLE | Delete rule |
| 25 | `reload_router_rules` | cap_router_mgr | CALLABLE | Reload rules |
| 26 | `inspect_image` | cap_llm_inspect | CALLABLE | Vision analysis |
| 27 | `tg_send_message` | cap_im_tg | CALLABLE | Send text |
| 28 | `tg_send_image` | cap_im_tg | CALLABLE | Send image |
| 29 | `tg_send_file` | cap_im_tg | CALLABLE | Send file |

**Tổng cộng: ~29 LLM-callable tools** trong hệ thống hiện tại (chưa tính IM channel capabilities khác và builtin tools 20 cái).

### 4B.5 Event Flow toàn hệ thống

```
IM Message (Telegram/Feishu/WeChat)
    │
    ▼
claw_event_t {
  event_type: "message"
  source_cap: "cap_im_tg"
  source_channel: "telegram"
  chat_id: "123456789"
  text: "bật đèn ở phòng khách"
}
    │
    ▼
claw_event_router.handle_event()
    │
    ├─ Rule: "im_any_message_agent"
    │   match: event_type="message", content_type="text"
    │   action: type="run_agent"
    │     → claw_core.submit(request)
    │
    │       claw_core agent loop:
    │       1. Inject context: system_prompt + memory + skills
    │       2. LLM says: call tool "gpio_write"
    │       3. claw_core.call_cap("gpio_write", {...})
    │       4. gpio module executes, returns result
    │       5. LLM receives result, decides next step
    │       6. Repeat until LLM returns final text
    │
    │       Final text: "Đã bật đèn ở phòng khách."
    │
    └─ Rule: "agent_response_telegram"
        match: event_type="agent_response"
        action: type="send_message"
          → cap_im_tg.tg_send_message(chat_id, text)
```

### 4B.6 Backend Integration Points

**Điểm kết nối với Supabase backend:**

| ESP Event | Backend Action |
|-----------|---------------|
| `device_pairing_request` | `request-otp` → publish MQTT OTP |
| `device_paired` | Update `devices.is_paired` → publish MQTT login_success |
| `neuron_pulse` | `POST /rest/v1/pulses` → trigger Supabase Edge Functions |
| `node_changed` | `POST /rest/v1/rpc/sync_neuron_delta` |
| `lua_script_executed` | `POST /rest/v1/script_executions` |
| `schedule_triggered` | Optional: notify via MQTT/backend |
| `agent_error` | Log to Supabase `agent_logs` table (needs creation) |

**Telegram Bot** (`cap_im_tg`): Nhận message từ user → router → agent → response. Không cần backend cho basic flow, nhưng Supabase backend cung cấp:
- Persistent memory cross-device
- Pattern detection & AI insights
- Device management dashboard
- NeuronLink sync

---

## 5. Flow Tổng thể (Backend + ESP)

```
┌─ ESP32 (First boot) ─────────────────────────────────────────────────────┐
│                                                                             │
│  1. generate_device_id()                                                    │
│     → Lấy WiFi STA MAC → SHA256 → base-36 → "GETAI-A3F2K"                  │
│                                                                             │
│  2. wifi_mgr_init_and_connect()                                             │
│     → NVS không có WiFi credentials                                         │
│     → Khởi động AP Portal: "ESP-A3F2K" / "12345678"                        │
│                                                                             │
│  3. User truy cập 192.168.4.1 → điền SSID/password WiFi nhà                │
│     → Lưu vào NVS → reboot                                                  │
│                                                                             │
│  4. WiFi kết nối thành công                                                 │
│                                                                             │
│  5. MQTT kết nối broker HiveMQ Cloud                                        │
│     → Subscribe: espclaw/GETAI-A3F2K/cmd                                    │
│     → Subscribe: espclaw/GETAI-A3F2K/otp                                    │
│     → Publish (LWT): espclaw/GETAI-A3F2K/status → "offline"                 │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
                                   │
                                   │ MQTT: online status
                                   ▼
┌─ Backend (Next.js API) ───────────────────────────────────────────────────┐
│                                                                             │
│  6. User gửi POST /api/devices/pair                                         │
│     { device_id: "GETAI-A3F2K", otp: "123456", tenant_id: "xxx" }           │
│                                                                             │
│  7. Backend:                                                                │
│     - Verify OTP (Redis, 5 phút TTL)                                       │
│     - Insert device vào Supabase:                                            │
│       { device_id, tenant_id, model: "ESP32-S3", esp32_mac: "xx:xx:xx" }   │
│     - Update device status: is_online = TRUE                                │
│     - Publish MQTT: espclaw/GETAI-A3F2K/login_success                        │
│                                                                             │
│  8. Supabase Trigger: cập nhật graph_stats                                  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
                                   │
                                   │ MQTT: login_success
                                   ▼
┌─ ESP32 (Sau khi pair) ────────────────────────────────────────────────────┐
│                                                                             │
│  9. Nhận MQTT message login_success                                        │
│  10. Cập nhật NVS: device paired, lưu tenant_id                            │
│  11. Bắt đầu NeuronLink sync: pull graph từ Supabase                       │
│      → GET /rest/v1/nodes?tenant_id=eq.xxx                                 │
│      → GET /rest/v1/links?tenant_id=eq.xxx                                 │
│      → Lưu vào NVS cache (max 100 nodes, 200 links)                        │
│  12. Cron service bắt đầu NTP sync                                          │
│  13. Agent sẵn sàng nhận tin nhắn                                           │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 5.1 Flow 1: Device Pairing — Thiết bị mới kết nối

(Download pairing diagram ở trên)

### 5.2 Flow 2: User gửi tin nhắn qua Telegram → ESP xử lý

```
┌─ Telegram User ──────────────────────────────────────────────────────────┐
│                                                                             │
│  Gửi tin nhắn: "Bật đèn phòng khách lên"                                   │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
                               │
                               │ Telegram Bot API (HTTPS)
                               ▼
┌─ ESP32 — Telegram Channel ────────────────────────────────────────────────┐
│                                                                             │
│  tg_poll_task (FreeRTOS, 5s timeout, limit=1):                            │
│    GET https://api.telegram.org/bot{token}/getUpdates                     │
│      ?offset={next_update_id}                                              │
│      &timeout=5                                                             │
│      &allowed_updates=message                                              │
│                                                                             │
│  → Parse update: {message: {chat: {id: 123456}, text: "Bật đèn..."}}     │
│  → Xây inbound_msg_t: {text, source=TELEGRAM, chat_id=123456}            │
│  → message_bus_post_inbound(bus, msg)                                      │
│                                                                             │
│  tg_flush_pending_updates() on boot → prevent replay                      │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
                               │
                               │ xQueueSend(inbound, msg, 0)
                               ▼
┌─ ESP32 — Agent Loop ─────────────────────────────────────────────────────┐
│                                                                             │
│  xQueueReceive(inbound, &msg, portMAX_DELAY)                              │
│       │                                                                     │
│       ▼                                                                    │
│  [1] ratelimit_check() → hourly 100, daily 1000                            │
│       │                                                                     │
│       ▼                                                                    │
│  [2] session_append("user", "Bật đèn phòng khách lên")                   │
│       │                                                                     │
│       ▼                                                                    │
│  [3] context_build_system_prompt()                                         │
│       → "You are ESPClaw v1.0..."                                          │
│       → "Connected to Telegram chat 123456"                                │
│       → 20 tools list                                                      │
│       │                                                                     │
│       ▼                                                                    │
│  [ROUND 1]                                                                  │
│  [4] LLM API Call (Anthropic):                                             │
│      POST /v1/messages                                                      │
│      {                                                                      │
│        "model": "claude-3-5-sonnet",                                        │
│        "max_tokens": 1024,                                                  │
│        "system": "<system_prompt>",                                         │
│        "tools": [<20 tools>],                                              │
│        "messages": [{"role":"user", "content": [{"type":"text", ...}]}]    │
│      }                                                                      │
│       │                                                                     │
│       ▼                                                                    │
│  LLM Response:                                                              │
│  {                                                                           │
│    "stop_reason": "tool_use",                                              │
│    "content": [{                                                             │
│      "type": "tool_use",                                                    │
│      "id": "toolu_01",                                                      │
│      "name": "gpio_write",                                                  │
│      "input": {"pin": 48, "state": 1}                                      │
│    }]                                                                       │
│  }                                                                          │
│       │                                                                     │
│       ▼                                                                    │
│  [5] try_dispatch_tool("gpio_write", {"pin":48,"state":1})                │
│       → hal_gpio_init() đã set GPIO48 as output                            │
│       → gpio_set_level(GPIO_NUM_48, 1)                                     │
│       → result = "OK: GPIO48=1"                                            │
│       │                                                                     │
│       ▼                                                                    │
│  [6] session_append_tool_use("toolu_01", "gpio_write", {pin:48,state:1})  │
│  [7] session_append_tool_result("toolu_01", "OK: GPIO48=1")                │
│                                                                             │
│  [ROUND 2]                                                                  │
│  [8] LLM API Call lần 2:                                                   │
│      messages = [user, assistant_tool_use, user_tool_result]              │
│       │                                                                     │
│       ▼                                                                    │
│  LLM Response:                                                             │
│  {                                                                           │
│    "stop_reason": "end_turn",                                              │
│    "content": [{ "type": "text", "text": "Đã bật đèn phòng khách (GPIO48). │
│                 Đèn hiệu sáng xanh trên board." }]                          │
│  }                                                                          │
│       │                                                                     │
│       ▼                                                                    │
│  [9] session_append("assistant", "Đã bật đèn...")                         │
│  [10] message_bus_post_outbound(bus, reply, TELEGRAM, chat_id=123456)    │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
                               │
                               │ xQueueSend(outbound, msg, 0)
                               ▼
┌─ ESP32 — Telegram Output Task ───────────────────────────────────────────┐
│                                                                             │
│  xQueueReceive(outbound, &msg, portMAX_DELAY)                              │
│       │                                                                     │
│       ▼                                                                    │
│  tg_send_message(123456, "Đã bật đèn...")                                 │
│  → HTTPS POST /bot{token}/sendMessage                                      │
│  { "chat_id": 123456, "text": "Đã bật đèn phòng khách (GPIO48). ..." }   │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
                               │
                               │ Telegram Bot API (HTTPS)
                               ▼
┌─ Telegram User ──────────────────────────────────────────────────────────┐
│                                                                             │
│  Nhận tin nhắn: "Đã bật đèn phòng khách (GPIO48). Đèn hiệu sáng xanh..."  │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

### 5.3 Flow 3: Supabase → ESP → Web Dashboard (Pulse)

```
┌─ Web Dashboard ───────────────────────────────────────────────────────────┐
│                                                                             │
│  User click vào node "Thời tiết" trên 3D graph                            │
│  → POST /api/neuron/pulse { source, target, type, energy }                │
│  → Supabase: INSERT INTO pulses                                           │
│  → Supabase Trigger: UPDATE nodes SET pulse_strength += energy*0.1        │
│  → Supabase Trigger: UPDATE links SET last_pulsed_at = NOW()             │
│  → Supabase Realtime: broadcast pulse event                               │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
                               │
                               │ Supabase Realtime (WebSocket)
                               ▼
┌─ Web Dashboard ───────────────────────────────────────────────────────────┐
│                                                                             │
│  onUpdate(payload):                                                        │
│  1. node.pulse_strength += 0.1 → getNodeSize() tăng → node hiển thị lớn hơn│
│  2. link.last_pulsed_at = now → link.animated = true                     │
│  3. graphRef.current.refresh() → cập nhật sprite + line objects          │
│  4. ParticleOverlay: thêm particle mới cho link                          │
│  5. setTimeout(60s) → link.animated = false → remove particle            │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
                               │
                               │ MQTT publish
                               ▼
┌─ ESP32 — NeuronLink ─────────────────────────────────────────────────────┐
│                                                                             │
│  MQTT espclaw/{device_id}/pulse topic                                     │
│  → nhận pulse event từ cloud                                             │
│  → agent có thể trigger action tự động dựa trên pulse                     │
│  → ví dụ: pulse từ "Thời tiết" → agent tự động gọi cron_schedule         │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

### 5.4 Flow 4: NeuronLink Sync — Đồng bộ Graph

```
┌─ ESP32 — NeuronLink Sync ───────────────────────────────────────────────┐
│                                                                             │
│ neuron_link_periodic_sync() // gọi mỗi 5 phút hoặc khi có trigger       │
│                                                                             │
│  // PUSH: ESP → Cloud                                                     │
│  1. Đọc NVS cache (nodes, links, pulses)                                  │
│  2. Lọc records thay đổi từ lần sync cuối (delta)                         │
│  3. POST /rest/v1/nodes?on_conflict=id                                    │
│     → Upsert các node mới/cập nhật                                        │
│  4. POST /rest/v1/links?on_conflict=id                                    │
│     → Upsert các link mới/cập nhật                                        │
│  5. POST /rest/v1/pulses                                                  │
│     → Insert các pulse record                                              │
│                                                                             │
│  // PULL: Cloud → ESP                                                     │
│  6. GET /rest/v1/nodes?tenant_id=eq.{tid}&updated_at=gt.{last_sync}      │
│  7. GET /rest/v1/links?tenant_id=eq.{tid}&updated_at=gt.{last_sync}      │
│  8. Merge vào NVS cache (ghi đè nếu cloud mới hơn)                        │
│  9. Cập nhật last_sync timestamp trong NVS                                │
│                                                                             │
│  // Conflict resolution: cloud wins (last-write-wins)                      │
│  // ESP có thể có cache cục bộ riêng → priority: Supabase > NVS cache   │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

### 5.5 Flow 5: OTA Firmware Update

```
┌─ ESP32 ─────────────────────────────────────────────────────────────────┐
│                                                                             │
│ User yêu cầu update firmware (qua web dashboard hoặc serial command)    │
│                                                                             │
│  1. Gọi tool: get_version() → "v1.0.0"                                   │
│  2. HTTP GET https://api.github.com/repos/{owner}/{repo}/releases/latest│
│  3. So sánh semver: latest="v1.1.0" → "v1.1.0" > "v1.0.0"                │
│  4. esp_https_ota(https_ota_config_t {                                  │
│       .url = "https://github.com/releases/v1.1.0/firmware.bin"           │
│     })                                                                    │
│  5. Verify signature (nếu có)                                            │
│  6. esp_restart() → boot vào partition mới                                │
│                                                                             │
└───────────────────────────────────────────────────────────────────────────┘
```

---

## 6. Lua Runtime & Script System

### 6.1 Kiến trúc Lua Runtime — Đã được triển khai đầy đủ

**PHÁT HIỆN QUAN TRỌNG:** ESPClaw có **production-grade Lua runtime** được tích hợp sẵn. Đây KHÔNG phải là đề xuất — đây là thực tế codebase. Phát hiện ban đầu "không có Lua script" trong codebase là SAI.

Dự án bao gồm:

| Thành phần | Đường dẫn | Mô tả |
|------------|-----------|--------|
| Lua Interpreter | `georgik/lua ^5.5.0~7` | Lua 5.5.x qua ESP-IDF component |
| Core Runtime | `components/claw_capabilities/cap_lua/src/` | 4 files C, ~3,500 lines |
| Lua Modules | `components/lua_modules/*/` | 17 modules C, mỗi module ~200-1000 lines |
| Lua Scripts | `components/lua_modules/*/lua_scripts/` | 13 file `.lua` mẫu |
| Skills Docs | `components/claw_capabilities/cap_lua/skills/` | 5 skill markdown files |

### 6.2 Kiến trúc 4 Lớp Lua

```
┌──────────────────────────────────────────────────────────────┐
│ LAYER 4: Lua Scripts (13 file .lua)                          │
│ builtin/  temp/  user/                                       │
├──────────────────────────────────────────────────────────────┤
│ LAYER 3: Cap Lua (8 capability tools)                       │
│ lua_list_scripts · lua_write_script · lua_run_script          │
│ lua_run_script_async · lua_list_async_jobs · lua_stop_async_job │
├──────────────────────────────────────────────────────────────┤
│ LAYER 2: Lua Runtime Engine (C)                             │
│ cap_lua_runtime.c    → Execution, timeout, output capture     │
│ cap_lua_async.c      → Job queue, 16 slots, 4 concurrent     │
│ cap_lua.c           → Storage, path validation, capabilities │
├──────────────────────────────────────────────────────────────┤
│ LAYER 1: Lua Interpreter                                  │
│ georgik/lua 5.5.x (dependency: georgik/lua: "^5.5.0~7")   │
│ Standard libs + custom C modules                             │
└──────────────────────────────────────────────────────────────┘
```

### 6.3 Async Job System — Chi tiết đầy đủ

```c
// Constants từ cap_lua_internal.h
#define CAP_LUA_MAX_SCRIPT_SIZE         (16 * 1024)   // 16KB max script
#define CAP_LUA_OUTPUT_SIZE             (4 * 1024)    // 4KB output buffer
#define CAP_LUA_SYNC_DEFAULT_TIMEOUT_MS 60000          // 60s sync timeout
#define CAP_LUA_ASYNC_DEFAULT_TIMEOUT_MS 0              // Unlimited async
#define CAP_LUA_ASYNC_MAX_JOBS          16             // Total slots
#define CAP_LUA_ASYNC_MAX_CONCURRENT     4              // Max running concurrently

// Job lifecycle:
typedef enum {
    CAP_LUA_JOB_QUEUED   = 0,  // Chờ trong queue
    CAP_LUA_JOB_RUNNING,         // Đang chạy (FreeRTOS task)
    CAP_LUA_JOB_DONE,            // Hoàn thành bình thường
    CAP_LUA_JOB_FAILED,          // Lỗi execution
    CAP_LUA_JOB_TIMEOUT,         // Timeout exceeded
    CAP_LUA_JOB_STOPPED,        // Bị stop cooperative
} cap_lua_job_status_t;
```

**Timeout Hook — Cooperative Cancellation:**
```c
// cap_lua_runtime.c: Timeout được poll qua Lua debug hook
// Mỗi 1000 Lua operations, hook được gọi:
static void cap_lua_timeout_hook(lua_State *L, lua_Debug *ar) {
    // 1. Check cooperative stop flag (volatile bool)
    if (*ctx->stop_requested) {
        luaL_error(L, "stopped by user");
    }
    // 2. Check wall-clock deadline
    if (esp_timer_get_time() > ctx->deadline_us) {
        luaL_error(L, "execution timed out");
    }
}
```

### 6.4 Module Registration

```c
// cap_lua.c: Boot sequence
esp_err_t cap_lua_register_group(const char *base_dir) {
    // 1. Set base directory (NVS hoặc flash partition)
    strlcpy(g_cap_lua_base_dir, base_dir, sizeof(g_cap_lua_base_dir));
    
    // 2. Register group với claw_cap registry
    return claw_cap_register_group(&s_lua_group);
}

// Module registration (mỗi lua_module_*.c gọi khi init):
esp_err_t lua_module_led_strip_register(void) {
    return cap_lua_register_module("led_strip", luaopen_led_strip);
}

// Khi script chạy `require("led_strip")`:
luaL_requiref(L, "led_strip", luaopen_led_strip, 1);
lua_pop(L, 1);  // Pop module (đã register vào package.loaded)
```

### 6.5 17 Lua Modules — Bảng tra cứu

| # | Module | C Functions | Lua API | Dùng cho |
|---|--------|------------|---------|-----------|
| 1 | `gpio` | 5 | `gpio.new(pin, dir)` | LED, relay, motor |
| 2 | `i2c` | 11 | `i2c.new(bus, sda, scl)` | BME280, OLED, sensors |
| 3 | `adc` | 6 | `adc.new(ch, pin)` | Voltage, light sensor |
| 4 | `button` | 9 | `button.new(cfg)` | Physical buttons |
| 5 | `delay` | 1 | `delay.delay_ms(ms)` | Timing |
| 6 | `system` | 7 | `system.time()` | Info |
| 7 | `storage` | 12 | `storage.read(path)` | File I/O |
| 8 | `uart` | 9 | `uart.new(cfg)` | Serial |
| 9 | `audio` | 13 | `audio.record()` | WAV record/play |
| 10 | `display` | 31 | `display.init()` | LCD graphics |
| 11 | `lcd_touch` | — | `lcd_touch.read()` | Touch input |
| 12 | `camera` | — | `camera.capture()` | JPEG capture |
| 13 | `led_strip` | 8 | `led_strip.new(pin, n)` | WS2812 LEDs |
| 14 | `mcpwm` | 9 | `mcpwm.new(cfg)` | Servo, motor |
| 15 | `event_publisher` | 5 | `event_publisher.publish()` | Channel events |
| 16 | `capability` | 2 | `capability.call(name, args)` | Call other caps |
| 17 | `esp_heap` | 4 | `esp_heap.get_info()` | Memory info |
| 18 | `board_manager` | 9 | `bm.get_display_lcd_params()` | Board config |

### 6.6 Ví dụ Script Thực tế

**LED Strip Rainbow (`basic_led_strip.lua`):**
```lua
local ls    = require("led_strip")
local delay = require("delay")

local LED_GPIO_NUM = args.pin or 38
local LED_COUNT    = args.num  or 16
local strip = ls.new(LED_GPIO_NUM, LED_COUNT)

strip:clear()
for i = 1, 3 do
    strip:set_pixel(0, 255, 255, 255)
    strip:refresh(); delay.delay_ms(150)
    strip:clear();    strip:refresh(); delay.delay_ms(150)
end

for offset = 0, 720, 8 do
    for i = 0, LED_COUNT - 1 do
        local hue = (i * 360 // LED_COUNT + offset) % 360
        strip:set_pixel_hsv(i, hue, 255, 64)
    end
    strip:refresh(); delay.delay_ms(40)
end
strip:clear(); strip:refresh()
```

**Audio Record & Play (`basic_audio_record_play.lua`):**
```lua
local audio   = require("audio")
local storage = require("storage")

local input  = audio.new_input(codec, 16000, 1, 16)
local output = audio.new_output(codec, 16000, 1, 16)
audio.set_volume(output, 100)
audio.play_tone(output, 523, 180, 100)  -- C5
delay.delay_ms(100)
audio.play_tone(output, 659, 180, 100)  -- E5
local info = audio.record_wav(input, "/sd/rec.wav", 3000)
audio.play_wav(output, info.path)
```

**Display Graphics (`basic_display_demo.lua`):**
```lua
local display = require("display")
local bm     = require("board_manager")

display.init(panel, io, width, height, panel_if)
display.begin_frame({ clear = true })
display.draw_rect(12, 12, w-24, h-24, 80, 120, 160)
display.fill_circle(110, 110, 18, 255, 160, 60)
display.draw_text_aligned(18, h-42, w-36, 20, "Hello", {...})
display.present()
```

### 6.7 Capability Tools cho Lua (8 tools)

| Tool | Mô tả | Input Schema |
|------|--------|-------------|
| `lua_list_scripts` | Liệt kê scripts theo prefix/keyword | `{prefix?, keyword?}` |
| `lua_write_script` | Ghi script (16KB max) | `{path, content, overwrite?}` |
| `lua_run_script` | Chạy sync (default 60s timeout) | `{path, args?, timeout_ms?}` |
| `lua_run_script_async` | Chạy async (unlimited, job_id trả về) | `{path, args?, name?, exclusive?, replace?}` |
| `lua_list_async_jobs` | Liệt kê async jobs | `{status?}` |
| `lua_get_async_job` | Chi tiết job | `{job_id?}` |
| `lua_stop_async_job` | Stop job cooperative | `{job_id?, name?, wait_ms?}` |
| `lua_stop_all_async_jobs` | Stop all jobs | `{exclusive?, wait_ms?}` |

### 6.8 Đề xuất Mở rộng

#### 6.8.1 Lua Script Marketplace

Thêm bảng vào Supabase để lưu trữ và quản lý scripts:

```sql
CREATE TABLE lua_scripts (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id UUID REFERENCES tenants(id),
  device_id UUID REFERENCES devices(id),
  name TEXT NOT NULL,
  path TEXT NOT NULL,
  content_hash TEXT NOT NULL,  -- SHA256
  size_bytes INTEGER NOT NULL,
  category TEXT DEFAULT 'general',
  is_public BOOLEAN DEFAULT FALSE,
  is_builtin BOOLEAN DEFAULT FALSE,
  downloads INTEGER DEFAULT 0,
  version INTEGER DEFAULT 1,
  created_at TIMESTAMPTZ DEFAULT NOW()
);
```

#### 6.8.2 Script Sync Protocol (ESP ↔ Cloud)

```c
// main/net/neuron_link.c - Thêm sync cho Lua scripts
typedef struct {
    char script_path[128];
    uint32_t checksum;        // SHA256
    uint8_t version;
    bool to_device;           // true: push, false: pull
} neuron_lua_sync_item_t;

void neuron_link_sync_lua_scripts(void) {
    // 1. Get local script checksums
    // 2. GET /rest/v1/lua_scripts?tenant_id=eq.{tid}
    // 3. Compare checksums, resolve conflicts
    // 4. Download/upload changed scripts
    // 5. Update NVS
}
```

#### 6.8.3 Enhanced Tool Definitions

```c
// Thêm vào builtin_tools.def cho Task/Automation system

TOOL_ENTRY("lua_list_scripts", "List Lua scripts",
  "{\"prefix\":\"str?\",\"keyword\":\"str?\"}",
  tool_lua_list_scripts)

TOOL_ENTRY("lua_run_script_async", "Run Lua script async",
  "{\"path\":\"str\",\"args\":\"obj?\",\"name\":\"str?\","
  "\"exclusive\":\"str?\",\"replace\":\"bool?\"}",
  tool_lua_run_script_async)

TOOL_ENTRY("lua_stop_async_job", "Stop Lua async job",
  "{\"job_id\":\"str?\",\"name\":\"str?\",\"wait_ms\":\"int?\"}",
  tool_lua_stop_async_job)
```

---

## 7. Đề xuất Cơ chế Node, Task Definition & Automation

### 8.1 Kiến trúc Node Types — Đề xuất mở rộng

Dựa trên schema Supabase hiện tại và cách agent hoạt động, đây là đề xuất **mở rộng node types** cho phù hợp với mạch ESP32-S3 và xử lý biên:

#### Phân loại Node Types (Mở rộng)

```typescript
// === CORE TYPES (đã có trong graph.ts) ===
type CoreNodeType =
  | 'user'           // Người dùng hệ thống
  | 'device'         // Thiết bị ESP32
  | 'skill'          // Kỹ năng / chức năng
  | 'memory'         // Bộ nhớ tri thức
  | 'tag'            // Tag phân loại
  | 'entity'         // Thực thể tổng quát
  | 'event'          // Sự kiện
  | 'goal'           // Mục tiêu
  | 'routine'        // Thói quen
  | 'insight'        // Insight/ý tưởng
  | 'pulse'          // Xung nơ-ron
  | 'webhook'        // Webhook integration
  | 'session'        // Phiên làm việc
  | 'context';       // Ngữ cảnh

// === ESP32-S3 SPECIFIC TYPES (mở rộng mới) ===
type ESPNodeType =
  | 'gpio_task'       // Task GPIO (bật/tắt đèn, relay, servo...)
  | 'sensor_read'     // Task đọc cảm biến (nhiệt độ, độ ẩm, ánh sáng...)
  | 'automation_rule' // Quy tắc tự động hóa (IF-THEN)
  | 'mqtt_publisher'  // Task publish MQTT message
  | 'http_request'    // Task HTTP request (GET/POST)
  | 'schedule_task'   // Task được lên lịch (cron)
  | 'i2c_device'      // Thiết bị I2C (OLED, cảm biến I2C)
  | 'firmware_config'  // Cấu hình firmware (WiFi, LLM, channel...)
  | 'data_logger'      // Task ghi log dữ liệu cảm biến
  | 'ota_update';      // Task OTA firmware

// === TASK-ORIENTED TYPES (cho task definition) ===
type TaskNodeType =
  | 'todo_task'        // Công việc cần làm
  | 'project'          // Dự án chứa nhiều task
  | 'checklist_item'   // Item trong checklist
  | 'recurring_task'   // Task lặp lại định kỳ
  | 'blocking_task'    // Task bị chặn bởi task khác
  | 'milestone';       // Mốc quan trọng trong dự án
```

#### Ví dụ Node cho ESP32-S3

```typescript
// Node: Đọc nhiệt độ từ BME280
const tempSensorNode: GraphNode = {
  id: 'esp-node-temp-001',
  name: 'BME280 Temperature',
  type: 'sensor_read',
  subtype: 'temperature',
  pos_x: 25,
  pos_y: 60,
  pos_z: 30,
  content: {
    i2c_address: '0x76',       // Địa chỉ I2C của BME280
    pin_sda: 1,                 // GPIO 1
    pin_scl: 2,                 // GPIO 2
    interval_ms: 60000,         // Đọc mỗi 60s
    unit: 'celsius',
    min_value: -40,
    max_value: 85,
    alert_threshold_high: 35,
    alert_threshold_low: 10,
    channel: 'telegram',        // Gửi alert qua kênh nào
    last_value: 26.5,
    last_read_at: '2026-04-29T10:00:00Z'
  }
};

// Node: Automation Rule
const automationNode: GraphNode = {
  id: 'esp-auto-001',
  name: 'Tưới cây tự động',
  type: 'automation_rule',
  subtype: 'if_this_then_that',
  pos_x: 50,
  pos_y: 50,
  pos_z: 50,
  content: {
    trigger: {
      type: 'time',
      cron: '0 7 * * *',        // Mỗi ngày 7h sáng
      description: 'Mỗi ngày 7:00'
    },
    conditions: [
      { type: 'sensor_value', node_id: 'esp-node-soil-001', operator: '<', value: 30 }
    ],
    actions: [
      { type: 'gpio_write', pin: 48, state: 1, duration_ms: 30000 },
      { type: 'gpio_write', pin: 48, state: 0 }
    ],
    enabled: true,
    last_triggered_at: null
  }
};

// Node: Task definition cho user
const todoNode: GraphNode = {
  id: 'task-001',
  name: 'Kiểm tra cảm biến BME280',
  type: 'todo_task',
  subtype: 'maintenance',
  pos_x: 30,
  pos_y: 70,
  pos_z: 20,
  content: {
    description: 'Kiểm tra cảm biến nhiệt độ trong phòng server',
    priority: 'high',           // low | medium | high | urgent
    due_date: '2026-05-01',
    assigned_to: 'device_esp_s3_01',
    checklist: [
      { id: 'cl-1', text: 'Kết nối BME280 qua I2C', done: true },
      { id: 'cl-2', text: 'Chạy tool wifi_scan để verify', done: false },
      { id: 'cl-3', text: 'Gửi test notification', done: false }
    ],
    dependencies: ['task-000'], // Task phụ thuộc
    tags: ['maintenance', 'sensor', 'esp32'],
    estimated_minutes: 30
  }
};
```

### 8.2 Đề xuất Link Types cho Task/Automation

```typescript
const TASK_LINK_TYPES = {
  // Task relationships
  'subtask_of':     { color: '#90A4AE', weight: 0.8 },  // Task con của task cha
  'blocks':         { color: '#B71C1C', weight: 1.0 },  // Task chặn task khác
  'depends_on':     { color: '#9C27B0', weight: 0.9 },  // Phụ thuộc vào
  'related_to':     { color: '#78909C', weight: 0.3 },  // Liên quan (yếu)

  // ESP ↔ Task relationships
  'triggers':       { color: '#FF9800', weight: 0.9 },  // Kích hoạt automation
  'monitors':       { color: '#2196F3', weight: 0.7 },  // Giám sát sensor
  'controls':       { color: '#4CAF50', weight: 1.0 },  // Điều khiển GPIO
  'reports_to':     { color: '#00BCD4', weight: 0.6 },  // Báo cáo về channel
  'logs_to':        { color: '#607D8B', weight: 0.5 },  // Ghi log vào

  // Data flow
  'reads_from':     { color: '#3F51B5', weight: 0.7 },  // Đọc từ sensor
  'writes_to':      { color: '#009688', weight: 0.7 },  // Ghi vào actuator
  'aggregates':     { color: '#673AB7', weight: 0.5 },  // Tổng hợp từ nhiều sensor
};
```

### 8.3 Lua Script Integration cho Automation

Nếu mục tiêu là thêm Lua script runtime vào ESP32-S3 để user dễ dàng định nghĩa task tự động, có **2 hướng tiếp cận**:

#### Hướng 1: Lua Interpreter nhẹ (e.g., eLua hoặc Lua 5.1 stripped)

```
┌─────────────────────────────────────────────────────────────────────────┐
│  ESP32-S3 Firmware với Lua Runtime                                      │
│                                                                          │
│  ┌─────────────────┐    ┌─────────────────────────────────────────┐    │
│  │  ESP-IDF C Core │◄──►│  Lua Interpreter (eLua / Lua 5.1)      │    │
│  │  (WiFi, TLS,    │    │                                         │    │
│  │   FreeRTOS,     │    │  User Lua Scripts:                      │    │
│  │   MQTT)         │    │  gpio.write(48, 1)                       │    │
│  │                 │    │  sensor.read("bme280", "temperature")    │    │
│  │  [C API Bridge] │    │  mqtt.publish("topic", "payload")        │    │
│  │                 │    │  cron.at("7:00", function() end)        │    │
│  │  UART/Queue     │    │                                         │    │
│  └─────────────────┘    └─────────────────────────────────────────┘    │
│                                                                          │
│  Communication: Shared memory queue hoặc UART framing                   │
│  Storage: Lua scripts lưu trong NVS partition                           │
│  Execution: Chạy trong dedicated FreeRTOS task hoặc cooperative task  │
└─────────────────────────────────────────────────────────────────────────┘
```

**Ưu điểm:**
- User có thể viết automation script dễ dàng (ngôn ngữ đơn giản)
- Không cần recompile firmware để thêm logic mới
- Interactive REPL qua Serial

**Nhược điểm:**
- Tốn ~30-50KB flash, ~8-16KB RAM cho Lua interpreter
- ESP32-S3 có đủ flash (8MB) nhưng C3 chỉ có 4MB
- Performance chậm hơn C native ~10-100x
- Thêm complexity cho security (Lua code injection)

**Các thư viện Lua cho ESP32:**

| Thư viện | Kích thước | Tính năng |
|---|---|---|
| eLua | ~200KB flash | Minimal, chỉ core Lua |
| NodeMCU Lua | ~250KB flash | Có file system, network libs |
| Lua 5.1 stripped | ~150KB flash | Minimal interpreter |
| Luau (Roblox Lua) | ~200KB flash | Type checking, faster |

#### Hướng 2: JSON-based Task Definition (Khuyến nghị)

Thay vì Lua script, dùng **JSON task definitions** được xử lý bởi agent C. User định nghĩa task bằng JSON qua Telegram/influx interface, agent parse và execute:

```json
// Ví dụ: Task định nghĩa bằng JSON (gửi qua Telegram)
{
  "task": "automation",
  "name": "Tưới cây tự động",
  "trigger": {
    "type": "cron",
    "expression": "0 7 * * *",
    "timezone": "Asia/Ho_Chi_Minh"
  },
  "conditions": [
    {
      "node": "esp-node-soil-001",
      "operator": "less_than",
      "value": 30
    }
  ],
  "actions": [
    {
      "tool": "gpio_write",
      "params": { "pin": 48, "state": 1 }
    },
    {
      "tool": "delay",
      "params": { "milliseconds": 30000 }
    },
    {
      "tool": "gpio_write",
      "params": { "pin": 48, "state": 0 }
    }
  ],
  "notify": {
    "channel": "telegram",
    "on_success": "Đã tưới cây thành công!",
    "on_failure": "Lỗi khi tưới cây: {error}"
  }
}
```

**Ưu điểm:**
- Không cần thêm Lua interpreter
- Parse bằng C JSON parser có sẵn
- User gửi task qua Telegram → agent parse → tạo cron task
- An toàn (không có code injection)
- Có thể lưu vào Supabase như node `automation_rule`

### 8.4 Đề xuất: Enhanced Tool Definitions cho Task System

```c
// Thêm vào builtin_tools.def — các tool mới cho task system

// Task/Automation tools
TOOL_ENTRY("task_create", "Create a new task with JSON definition",
  "{\"name\":\"str\",\"type\":\"str\",\"description\":\"str\","
  "\"priority\":\"str\",\"due_date\":\"str\",\"tags\":\"list\"}",
  tool_task_create)

TOOL_ENTRY("task_list", "List all tasks with optional filters",
  "{\"status\":\"str?\",\"priority\":\"str?\",\"tag\":\"str?\"}",
  tool_task_list)

TOOL_ENTRY("task_update", "Update task status or fields",
  "{\"task_id\":\"str\",\"updates\":\"obj\"}",
  tool_task_update)

TOOL_ENTRY("automation_create", "Create automation rule with trigger + actions",
  "{\"name\":\"str\",\"trigger\":\"obj\",\"conditions\":\"list?\","
  "\"actions\":\"list\",\"notify\":\"obj?\"}",
  tool_automation_create)

TOOL_ENTRY("automation_list", "List all automation rules",
  "{\"enabled\":\"bool?\"}",
  tool_automation_list)

TOOL_ENTRY("automation_toggle", "Enable or disable automation rule",
  "{\"automation_id\":\"str\",\"enabled\":\"bool\"}",
  tool_automation_toggle)

// Sensor tools
TOOL_ENTRY("sensor_read", "Read from I2C sensor",
  "{\"address\":\"str\",\"register\":\"int\",\"bytes\":\"int\"}",
  tool_sensor_read)

TOOL_ENTRY("sensor_bme280_read", "Read BME280 temperature/humidity/pressure",
  "{\"address\":\"str?\"}",
  tool_bme280_read)

// Data logging tools
TOOL_ENTRY("log_write", "Write sensor data to log",
  "{\"key\":\"str\",\"value\":\"str\"}",
  tool_log_write)

TOOL_ENTRY("log_read", "Read recent log entries",
  "{\"key\":\"str?\",\"limit\":\"int?\"}",
  tool_log_read)
```

### 8.5 Đề xuất: Cơ chế User định nghĩa Task dễ dàng

**Interface 1: Natural Language → Structured Task (Agent)**

```
User: "Mỗi ngày 7h sáng, nếu độ ẩm đất < 30% thì bật máy bơm GPIO48 trong 30 giây, rồi tắt"
         ↓ Agent (LLM)
{
  "task": "automation",
  "name": "Tưới cây tự động",
  "trigger": { "type": "cron", "expression": "0 7 * * *" },
  "conditions": [
    { "node": "soil-sensor-01", "operator": "<", "value": 30 }
  ],
  "actions": [
    { "tool": "gpio_write", "params": { "pin": 48, "state": 1 }},
    { "tool": "delay", "params": { "milliseconds": 30000 }},
    { "tool": "gpio_write", "params": { "pin": 48, "state": 0 }}
  ]
}
```

**Interface 2: Structured JSON (cho advanced users)**

```
User gửi JSON trực tiếp qua Telegram (dùng code block):
```json
{
  "task": "automation",
  "name": "Alert nhiệt độ cao",
  "trigger": { "type": "interval", "seconds": 300 },
  "conditions": [
    { "type": "sensor_value", "sensor_id": "temp-01", "operator": ">", "value": 35 }
  ],
  "actions": [
    { "tool": "telegram_send", "params": { "text": "⚠️ Nhiệt độ cao: {value}°C" }}
  ]
}
```
```

**Interface 3: Voice/Talking (Agent tự parse)**

```
User: "Ê ESP, nhắc tôi kiểm tra cảm biến mỗi tuần vào thứ 2"
         ↓ Agent
→ Tạo node task "Kiểm tra cảm biến" với cron weekly Monday
→ Gửi reminder notification mỗi thứ 2
```

---

## 8. Ví dụ Code Mẫu — 3D Graph Nâng cao

### 8.1 Ví dụ 1: Custom Node — Octahedron với Label

```typescript:components/KnowledgeGraph3D.tsx
// Custom node: Hình đa diện + nhãn text sprite
const nodeThreeObject = useCallback((node: GraphNode) => {
  const size = getNodeSize(node);

  // === Tạo geometry dựa trên type ===
  let geometry;
  switch (node.type) {
    case 'device':
      geometry = new THREE.BoxGeometry(size * 1.2, size * 1.2, size * 1.2);
      break;
    case 'skill':
      geometry = new THREE.OctahedronGeometry(size);
      break;
    case 'event':
      geometry = new THREE.TetrahedronGeometry(size);
      break;
    case 'goal':
      geometry = new THREE.IcosahedronGeometry(size);
      break;
    default:
      geometry = new THREE.SphereGeometry(size * 0.8, 8, 8);
  }

  // === Material với emissive glow ===
  const material = new THREE.MeshPhongMaterial({
    color: getNodeColor(node),
    emissive: getNodeColor(node),
    emissiveIntensity: node.pulse_strength * 0.5,
    transparent: true,
    opacity: 0.85,
    shininess: 80,
    specular: new THREE.Color(0x333333),
  });

  const mesh = new THREE.Mesh(geometry, material);

  // === Label sprite (chỉ hiện khi zoom đủ lớn) ===
  const labelCanvas = document.createElement('canvas');
  labelCanvas.width = 256;
  labelCanvas.height = 64;
  const ctx = labelCanvas.getContext('2d');
  ctx.fillStyle = 'rgba(0,0,0,0.7)';
  ctx.roundRect(0, 0, 256, 64, 8);
  ctx.fill();
  ctx.fillStyle = '#ffffff';
  ctx.font = 'bold 20px Inter, sans-serif';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillText(node.name, 128, 32);

  const labelSprite = new THREE.Sprite(
    new THREE.SpriteMaterial({
      map: new THREE.CanvasTexture(labelCanvas),
      transparent: true,
      opacity: 0,  // Ẩn mặc định, hiện khi zoom
    })
  );
  labelSprite.scale.set(size * 8, size * 2, 1);
  labelSprite.position.y = size + 3;
  mesh.add(labelSprite);

  // === Pulse ring animation ===
  if (node.pulse_strength > 0) {
    const ringGeo = new THREE.RingGeometry(size * 1.5, size * 2, 32);
    const ringMat = new THREE.MeshBasicMaterial({
      color: getNodeColor(node),
      transparent: true,
      opacity: node.pulse_strength * 0.5,
      side: THREE.DoubleSide,
    });
    const ring = new THREE.Mesh(ringGeo, ringMat);
    ring.rotation.x = Math.PI / 2;
    mesh.add(ring);
  }

  return mesh;
}, []);

// Ẩn/hiện label dựa trên camera distance
useEffect(() => {
  const graph = graphRef.current;
  if (!graph) return;

  graph.onEngineStop(() => {
    const camera = graph.camera();
    graph.graphData().nodes.forEach(node => {
      const obj = node.__threeObj;
      if (obj && obj.children) {
        const label = obj.children.find(c => c.isSprite);
        if (label) {
          const distance = camera.position.distanceTo(obj.position);
          label.material.opacity = distance < 200 ? 1 : 0;
        }
      }
    });
  });
}, []);
```

### 8.2 Ví dụ 2: Post-Processing Bloom Effect

```typescript
// Thêm bloom effect cho glow nổi bật hơn
const initPostProcessing = () => {
  const composer = graphRef.current.postProcessingComposer();
  if (!composer) return;

  // Tạo EffectComposer pipeline
  const bloomPass = new EffectComposer(null);
  bloomPass.addPass(new RenderPass(scene, camera));

  const unrealBloomPass = new UnrealBloomPass(
    new THREE.Vector2(window.innerWidth, window.innerHeight),
    1.5,    // Bloom strength
    0.4,    // Radius
    0.85    // Threshold — chỉ pixel > 0.85 mới bloom
  );
  bloomPass.addPass(unrealBloomPass);

  // Điều chỉnh bloom dựa trên pulse strength
  const animateBloom = () => {
    const avgPulse = graphData.nodes.reduce((s, n) => s + n.pulse_strength, 0)
      / graphData.nodes.length;
    unrealBloomPass.strength = 0.5 + avgPulse * 2;  // 0.5 - 2.5
    requestAnimationFrame(animateBloom);
  };
  animateBloom();
};
```

### 8.3 Ví dụ 3: DAG Tree Layout với 3D Force Graph

```typescript
// Hiển thị task hierarchy dưới dạng cây DAG 3D
const TaskDAGGraph = ({ tasks }: { tasks: Task[] }) => {
  const graphRef = useRef<any>(null);

  // Convert tasks → graph format với DAG layout hints
  const graphData = useMemo(() => {
    const nodes = tasks.map((task, i) => {
      const level = getDAGLevel(task);  // 0 = root, 1 = subtask, 2 = deep
      const angle = (i / tasks.length) * Math.PI * 2;
      return {
        ...task,
        // Radial layout: mỗi level trên một vòng tròn khác nhau
        x: Math.cos(angle) * (level + 1) * 150,
        y: level * -100,  // Level 0 ở trên, tăng xuống dưới
        z: Math.sin(angle) * (level + 1) * 150,
        // Fixed position cho DAG
        fx: Math.cos(angle) * (level + 1) * 150,
        fy: level * -100,
        fz: Math.sin(angle) * (level + 1) * 150,
      };
    });

    const links = tasks.flatMap(task =>
      (task.dependencies || []).map(depId => ({
        source: depId,
        target: task.id,
        type: 'depends_on',
        curve_strength: 0.3,
        animated: false,
      }))
    );

    return { nodes, links };
  }, [tasks]);

  return (
    <ForceGraph3D
      ref={graphRef}
      graphData={graphData}
      nodeThreeObject={nodeThreeObject}
      linkThreeObject={linkThreeObject}
      // DAG layout mode
      dagMode="radialout"
      dagLevelDistance={120}
      // Disable force simulation (DAG đã có vị trí cố định)
      d3AlphaDecay={0}
      warmupTicks={0}
      cooldownTicks={0}
      // Interaction
      enableNodeDrag={false}
      controlType="orbit"
      // Camera
      cameraPosition={{ x: 0, y: 200, z: 500 }}
    />
  );
};
```

### 8.4 Ví dụ 4: Real-time Updates — Supabase Realtime Integration

```typescript
// Dashboard component với real-time sync
'use client';

import { createClient } from '@supabase/supabase-js';
import dynamic from 'next/dynamic';

const ForceGraph3D = dynamic(() => import('react-force-graph-3d'), { ssr: false });

const supabase = createClient(SUPABASE_URL, SUPABASE_ANON_KEY);

export default function LiveGraphDashboard({ tenantId }: { tenantId: string }) {
  const [graphData, setGraphData] = useState({ nodes: [], links: [] });
  const graphRef = useRef<any>(null);

  // === Initial load ===
  useEffect(() => {
    fetchGraphData(tenantId).then(setGraphData);
  }, [tenantId]);

  // === Real-time subscription ===
  useEffect(() => {
    const channel = supabase
      .channel(`graph-live-${tenantId}`)
      .on('postgres_changes', {
        event: 'INSERT',
        schema: 'public',
        table: 'pulses',
        filter: `tenant_id=eq.${tenantId}`
      }, async (payload) => {
        const pulse = payload.new as Pulse;

        // Cập nhật node target: tăng pulse_strength
        setGraphData(prev => ({
          ...prev,
          nodes: prev.nodes.map(n =>
            n.id === pulse.target_node_id
              ? { ...n, pulse_strength: Math.min(1, n.pulse_strength + 0.1) }
              : n
          )
        }));

        // Bật animated cho link liên quan
        setGraphData(prev => ({
          ...prev,
          links: prev.links.map(l =>
            l.source === pulse.source_node_id && l.target === pulse.target_node_id
              ? { ...l, animated: true, last_pulsed_at: new Date().toISOString() }
              : l
          )
        }));

        // Thêm particle vào overlay
        addParticle(pulse.source_node_id, pulse.target_node_id);

        // Auto-off sau 60s
        setTimeout(() => {
          setGraphData(prev => ({
            ...prev,
            links: prev.links.map(l =>
              l.id === l.source + '-' + l.target
                ? { ...l, animated: false }
                : l
            )
          }));
        }, 60000);

        // Refresh 3D objects
        graphRef.current?.refresh();
      })
      .on('postgres_changes', {
        event: '*',
        schema: 'public',
        table: 'nodes',
        filter: `tenant_id=eq.${tenantId}`
      }, async (payload) => {
        const node = payload.new;
        if (payload.eventType === 'INSERT') {
          setGraphData(prev => ({
            ...prev,
            nodes: [...prev.nodes, transformNode(node)]
          }));
        } else if (payload.eventType === 'UPDATE') {
          setGraphData(prev => ({
            ...prev,
            nodes: prev.nodes.map(n => n.id === node.id ? transformNode(node) : n)
          }));
        } else if (payload.eventType === 'DELETE') {
          setGraphData(prev => ({
            ...prev,
            nodes: prev.nodes.filter(n => n.id !== payload.old.id)
          }));
        }
        graphRef.current?.refresh();
      })
      .subscribe();

    return () => {
      supabase.removeChannel(channel);
    };
  }, [tenantId]);

  return (
    <div className="relative w-full h-screen bg-gray-950">
      <ForceGraph3D
        ref={graphRef}
        graphData={graphData}
        nodeThreeObject={nodeThreeObject}
        linkThreeObject={linkThreeObject}
        nodeLabel={(node) => `<div class="tooltip">${node.name}</div>`}
        onNodeClick={(node) => {
          // Zoom vào node
          graphRef.current?.centerAt(node.x, node.y, 1000);
          graphRef.current?.zoom(2.5, 1000);
        }}
        // Physics
        d3VelocityDecay={0.3}
        warmupTicks={100}
        // Visual
        backgroundColor="#0a0a0f"
        showNavInfo={true}
        enableNavigationControls={true}
        controlType="orbit"
      />
      <ParticleOverlay particles={activeParticles} dimensions={dims} />
    </div>
  );
}
```

### 8.5 Ví dụ 5: Dynamic Force Simulation — Theo dõi Task Dependencies

```typescript
// Tùy chỉnh d3-force để visualize dependencies
const configureForces = (graphRef) => {
  const fg = graphRef.current;
  if (!fg) return;

  // Force từ tính (liên kết)
  const linkForce = fg.d3Force('link');
  linkForce.strength(link => {
    if (link.type === 'depends_on') return -0.5;  // Yếu
    if (link.type === 'blocks') return -2;         // Mạnh
    return -0.3;                                    // Thường
  });
  linkForce.distance(80);

  // Force đẩy (charge) — nodes đẩy nhau
  const chargeForce = fg.d3Force('charge');
  chargeForce.strength(-200);     // Repulsion mạnh
  chargeForce.distanceMax(500);   // Chỉ ảnh hưởng trong 500 units

  // Force hấp dẫn về tâm
  const centerForce = fg.d3Force('center');
  centerForce.strength(0.05);     // Yếu — cho phép phân tán

  // Force theo trục Y (gravity plane)
  fg.d3Force('y', d3.forceY(0).strength(0.02));

  // Reheat simulation
  fg.d3ReheatSimulation();
};
```

---

## 9. Bảng Tra Cứu Nhanh

### 9.1 GPIO Pin Mapping (ESP32-S3 DevKit)

```
Analog / Special:
  GPIO 0-21    → Digital GPIO (input/output)
  GPIO 1/2     → I2C SDA/SCL (mặc định cho OLED, BME280)
  GPIO 48      → GPIO48 (OUTPUT — đèn, relay, motor)
  GPIO 38-40   → Reserved (USB D-/D+)

Power:
  3V3 pin      → 3.3V output (max 1A)
  GND          → Ground
  EN           → Enable (pull HIGH để chạy)
  GPIO 0       → Boot mode (HIGH = normal, LOW = download)
```

### 9.2 Supabase API Endpoints

```
Tables:
  GET/POST      /rest/v1/devices
  GET/PATCH/DEL /rest/v1/devices?id=eq.{id}
  GET/POST      /rest/v1/nodes?tenant_id=eq.{tid}&is_active=eq.true
  GET/POST      /rest/v1/links?tenant_id=eq.{tid}&is_active=eq.true
  POST          /rest/v1/pulses

Functions:
  POST /rest/v1/rpc/sync_neuron_delta  (bulk upsert)
  POST /rest/v1/rpc/create_pulse       (trigger automation)
  POST /rest/v1/rpc/get_graph_stats    (aggregated stats)
```

### 9.3 MQTT Topics

```
espclaw/{device_id}/cmd           ← ESP subscribe (cloud → ESP)
espclaw/{device_id}/otp           ← ESP subscribe (OTP code)
espclaw/{device_id}/login_success ← ESP subscribe (pairing confirmation)
espclaw/{device_id}/response      → ESP publish (agent response)
espclaw/{device_id}/status        → ESP publish (LWT: online/offline)

neuron/{tenant_id}/update         → Dashboard subscribe (node/link changes)
neuron/{tenant_id}/pulse          → Dashboard subscribe (pulse events)
```

### 9.4 Cron Expression Reference

```
┌─────────────┬──────────────┐
│ Field        │ Values       │
├─────────────┼──────────────┤
│ minute       │ 0-59         │
│ hour         │ 0-23         │
│ day-of-month │ 1-31         │
│ month        │ 1-12         │
│ day-of-week  │ 0-6 (0=Sun)  │
└─────────────┴──────────────┘
Ví dụ:
  "0 7 * * *"    → Mỗi ngày 7:00 AM
  "30 8 * * 1"   → Mỗi thứ 2 8:30 AM
  "*/15 * * * *" → Mỗi 15 phút
  "0 9,18 * * *" → 9:00 AM và 6:00 PM
```

### 9.5 Tool Response Format

```json
// Thành công
{
  "success": true,
  "result": "OK: GPIO48=1",
  "execution_time_ms": 5
}

// Lỗi
{
  "success": false,
  "error": "GPIO pin 100 out of range [0-48]"
}
```

### 9.6 Memory Layout (NVS Namespaces)

```
espclaw           → WiFi credentials, device paired flag, tenant_id
espclaw.persona   → current_persona, system_prompt
espclaw.cron      → Scheduled tasks (JSON array)
espclaw.ratelimit → hourly_count, daily_count, day_of_year
espclaw.sync      → last_sync_timestamp
espclaw.tg        → bot_token, allowed_chat_ids, update_offset
espclaw.device    → device_id, esp32_mac, firmware_version
espclaw.neuron    → cached nodes/links (limited size)
```

### 9.7 ESP-IDF Kconfig Options

```
CONFIG_ESPCLAW_WIFI_SSID
CONFIG_ESPCLAW_WIFI_PASS
CONFIG_ESPCLAW_LLM_PROVIDER=openai|anthropic
CONFIG_ESPCLAW_LLM_MODEL=gpt-4o-mini|claude-3-5-haiku
CONFIG_ESPCLAW_LLM_API_KEY
CONFIG_ESPCLAW_MQTT_BROKER_URL
CONFIG_ESPCLAW_MQTT_USERNAME
CONFIG_ESPCLAW_MQTT_PASSWORD
CONFIG_ESPCLAW_TELEGRAM_BOT_TOKEN
CONFIG_ESPCLAW_CHANNEL_SERIAL=y
CONFIG_ESPCLAW_CHANNEL_TELEGRAM=y|n
CONFIG_ESPCLAW_CHANNEL_MQTT=y|n
CONFIG_ESPCLAW_CHANNEL_DINGTALK=y|n
CONFIG_ESPCLAW_CHANNEL_DISCORD=y|n
CONFIG_ESPCLAW_GPIO_MIN_PIN=0
CONFIG_ESPCLAW_GPIO_MAX_PIN=48
CONFIG_ESPCLAW_GPIO_ALLOWED_PINS_CSV="1,2,48"
CONFIG_ESPCLAW_TARGET_ESP32S3=y
CONFIG_ESPCLAW_PARTITION_TABLE_SINGLE_APP=y
```

---

*Tài liệu được tổng hợp từ phân tích source code ESPClaw. Mọi chi tiết kỹ thuật dựa trên mã nguồn thực tế trong codebase.*
