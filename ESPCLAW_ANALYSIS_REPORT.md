# ESPClaw - Báo Cáo Phân Tích Kỹ Thuật Chi Tiết

> **Phiên bản:** 1.0.0  
> **Ngày phân tích:** 6/5/2026  
> **Phần cứng mục tiêu:** ESP32-S3 N16R8 (16MB Flash + 8MB PSRAM)  
> **Tác giả:** AI Technical Analysis

---

## 📋 Mục Lục

1. [Tổng Quan Dự Án](#1-tổng-quan-dự-án)
2. [Kiến Trúc Hệ Thống](#2-kiến-trúc-hệ-thống)
3. [Firmware ESP32-S3 - Phân Tích Chi Tiết](#3-firmware-esp32-s3---phân-tích-chi-tiết)
4. [Backend & Web Dashboard](#4-backend--web-dashboard)
5. [Luồng Tương Tác Device ↔ Backend](#5-luồng-tương-tác-device--backend)
6. [Công Nghệ & Stack](#6-công-nghệ--stack)
7. [Các Tính Năng Chính](#7-các-tính-năng-chính)
8. [Bảo Mật & Tối Ưu](#8-bảo-mật--tối-ưu)

---

## 1. Tổng Quan Dự Án

### 1.1. Giới Thiệu

**ESPClaw** là một dự án AI Assistant Firmware hoàn chỉnh chạy trên vi điều khiển ESP32 giá rẻ ($2-5), được viết hoàn toàn bằng **Pure C** (không C++). Dự án tích hợp:

- ✅ **ReAct Agent Loop** - Vòng lặp suy luận và hành động
- ✅ **20 Built-in Tools** - GPIO, Memory, Cron, Network, System
- ✅ **10 Communication Channels** - Serial, Telegram, MQTT, Discord, Slack...
- ✅ **Multi-LLM Support** - OpenAI, Anthropic, Ollama, Custom endpoints
- ✅ **Cloud Sync** - Supabase PostgreSQL + 3D Knowledge Graph
- ✅ **Workflow Engine** - Tự động hóa kịch bản không cần LLM

### 1.2. Thông Số Kỹ Thuật

| Thông Số | Giá Trị |
|----------|---------|
| **Firmware Size** | ~920KB (78% Flash còn trống) |
| **Lines of Code** | 8,478 dòng C thuần |
| **RAM Usage (S3)** | SRAM: 512KB, PSRAM: 8MB |
| **Buffer Sizes** | Request: 24KB, Response: 24KB (PSRAM) |
| **Session History** | 24 turns (S3) / 8 turns (C3) |
| **Tool Rounds** | Max 10 rounds/request (S3) |
| **Rate Limit** | 100/hour, 1000/day |

### 1.3. Supported Hardware

```
ESP32-S3 N16R8 (Recommended)
├── Flash: 16MB
├── PSRAM: 8MB Octal
├── Cores: 2 (Xtensa LX7)
├── WiFi: 802.11 b/g/n
├── Bluetooth: BLE 5.0
└── GPIO: 45 pins

Boards:
- Seeed XIAO ESP32S3 Plus (16MB+8MB) ⭐
- Seeed XIAO ESP32S3 Sense (8MB+8MB+Camera+SD)
- ESP32-S3 DevKit
- Generic ESP32-C3/C5 (4MB, no PSRAM)
```

---

## 2. Kiến Trúc Hệ Thống

### 2.1. Sơ Đồ Tổng Thể

```
┌─────────────────────────────────────────────────────────────────────┐
│                         USER INTERFACES                              │
│  Telegram Bot │ Web Dashboard │ Serial CLI │ MQTT Client            │
└────────┬────────────────┬────────────┬──────────────┬───────────────┘
         │                │            │              │
         ▼                ▼            ▼              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      TRANSPORT LAYER                                 │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  Next.js API Routes (Edge Functions)                         │   │
│  │  /api/device/request-otp  │  /api/workflows  │  /api/graph  │   │
│  └────────────────────┬─────────────────────────────────────────┘   │
│                       │                                              │
│  ┌────────────────────▼─────────────────────────────────────────┐   │
│  │  Supabase PostgreSQL + pgvector + RLS                        │   │
│  │  Tables: devices, nodes, links, pulses, workflows, personas  │   │
│  └────────────────────┬─────────────────────────────────────────┘   │
│                       │                                              │
│  ┌────────────────────▼─────────────────────────────────────────┐   │
│  │  MQTT Broker (HiveMQ Cloud TLS 8883)                         │   │
│  │  Topics: espclaw/{device_id}/{cmd|otp|response|status}       │   │
│  └────────────────────┬─────────────────────────────────────────┘   │
└───────────────────────┼──────────────────────────────────────────────┘
                        │
┌───────────────────────▼──────────────────────────────────────────────┐
│                    ESP32 FIRMWARE LAYER                              │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │              REACT AGENT LOOP (FreeRTOS Core 1)              │   │
│  │                                                              │   │
│  │  [Inbound Queue] → Rate Limit → Session Append              │   │
│  │         ↓                                                    │   │
│  │  System Prompt Build → LLM API Call (OpenAI/Anthropic)      │   │
│  │         ↓                                                    │   │
│  │  Tool Dispatch? → YES: Execute Tool → Loop                  │   │
│  │                → NO:  Send to Outbound Queue                │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  TOOL REGISTRY (20 tools)                                    │   │
│  │  gpio_write, memory_set, cron_schedule, wifi_scan...        │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  CHANNEL SYSTEM (10 channels)                                │   │
│  │  Serial, Telegram, MQTT, Discord, Slack, DingTalk...        │   │
│  └──────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  NEURON LINK (Cloud Sync Protocol)                           │   │
│  │  Delta Sync, Full Sync, Workflow Sync, Config Sync          │   │
│  └──────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────┘
```

### 2.2. Luồng Dữ Liệu Chính

```
User Input (Telegram/MQTT/Serial)
    │
    ▼
Channel Input Task → Inbound Queue (FreeRTOS)
    │
    ▼
Agent Task (Core 1)
    ├─→ Rate Limit Check (100/hour, 1000/day)
    ├─→ Session Append (Ring Buffer 24 turns)
    ├─→ Build System Prompt (Tools + Persona + Context)
    │
    ▼
LLM API Call (TLS Mutex Lock)
    │
    ├─→ Tool Use? → Tool Registry Dispatch
    │       │
    │       ├─→ gpio_write(pin, state)
    │       ├─→ memory_set(key, value)
    │       ├─→ cron_schedule(...)
    │       └─→ Result → Session Append → Loop
    │
    └─→ Text Response → Outbound Queue
            │
            ▼
Channel Output Task → User (Telegram/MQTT/Serial)
```

---

## 3. Firmware ESP32-S3 - Phân Tích Chi Tiết

### 3.1. Khởi Tạo Hệ Thống (`main/main.c`)

```c
void app_main(void) {
    // 1. Generate Device ID từ MAC address
    generate_device_id();  // → "GETAI-XXXXX" (base-36)
    
    // 2. OLED UI Init (non-fatal)
    oled_ui_init();
    
    // 3. NVS Flash Init
    nvs_mgr_init();
    
    // 4. TLS Mutex + Rate Limiter + Persona
    espclaw_tls_init();
    ratelimit_init();
    persona_init();
    
    // 5. Message Bus (Inbound/Outbound Queues)
    message_bus_init(&s_bus);
    
    // 6. Agent Start (Core 1, Priority 5)
    agent_start(&s_bus);
    
    // 7. Cron Service Init
    cron_init();
    
    // 8. WiFi Connect (hoặc AP Config Portal)
    wifi_mgr_init_and_connect();
    
    // 9. LLM Provider Registry
    provider_registry_init();
    
    // 10. GPIO HAL Init
    hal_gpio_init();
    
    // 11. Neuron Link Init (Supabase Sync)
    neuron_link_init(&nl_config);
    
    // 12. Cron Start
    cron_start(s_bus.inbound);
    
    // 13. Channel Registry (Serial, Telegram, MQTT...)
    channel_registry_init();
    channel_start_all(&s_bus);
    
    // 14. HTTP Server (Config Portal)
    http_server_start();
}
```

**Thứ tự khởi tạo quan trọng:**
1. Agent task được start **sớm** để chiếm Internal RAM cho stack
2. Message bus init trước agent để tránh OOM
3. WiFi connect có thể fail → fallback sang AP Config Portal
4. Neuron Link chỉ sync nếu đã paired (có tenant_id)

### 3.2. Agent Loop - ReAct Algorithm

**File:** `main/agent/agent_loop.c`

```c
static void agent_task(void *arg) {
    // Allocate buffers từ PSRAM (S3) hoặc SRAM (C3)
    char *msgs_json   = ESPCLAW_MALLOC(24576);  // 24KB
    char *tools_json  = ESPCLAW_MALLOC(24576);
    char *reply       = ESPCLAW_MALLOC(24576);
    char *sys_prompt  = ESPCLAW_MALLOC(4096);
    
    // Session history (ring buffer)
    session_init(s_session);
    
    // Tool registry init
    tool_registry_init();  // 20 tools
    
    // Workflow engine init
    workflow_engine_init();
    
    while (1) {
        // 1. Đợi tin nhắn từ inbound queue
        xQueueReceive(s_bus->inbound, &in, portMAX_DELAY);
        
        // 2. Rate limit check
        if (!ratelimit_check(rl_reason, sizeof(rl_reason))) {
            message_bus_post_outbound(s_bus, &out, ...);
            continue;
        }
        
        // 3. Workflow-first routing
        const workflow_t *matched = workflow_engine_match(in.text);
        if (matched) {
            workflow_engine_execute(matched, in.text, &exec);
            message_bus_post_outbound(s_bus, &out, ...);
            continue;  // Skip LLM nếu workflow match
        }
        
        // 4. Append user message vào session
        session_append(s_session, "user", in.text);
        
        // 5. Build system prompt
        context_build_system_prompt(sys_prompt, ...);
        
        // 6. ReAct loop (max 10 rounds)
        for (int round = 0; round < MAX_TOOL_ROUNDS; round++) {
            // Build messages JSON (Anthropic hoặc OpenAI format)
            session_build_messages_json(s_session, msgs_json, ...);
            
            // Build tools JSON (20 tools)
            tool_registry_build_tools_json(tools_json, ...);
            
            // Call LLM API (TLS mutex lock)
            llm->complete(sys_prompt, msgs_json, tools_json, reply, ...);
            
            // Parse response
            if (try_dispatch_tool(reply, tool_id, tool_name, tool_input, tool_result)) {
                // Tool use detected → execute → append result → continue loop
                session_append_tool_use(s_session, tool_id, tool_name, tool_input);
                session_append_tool_result(s_session, tool_id, tool_result);
                continue;
            }
            
            // Plain text response → done
            session_append(s_session, "assistant", reply);
            message_bus_post_outbound(s_bus, &out, ...);
            break;
        }
    }
}
```

**Đặc điểm:**
- ✅ **Workflow-first:** Kiểm tra workflow trước, chỉ gọi LLM nếu không match
- ✅ **Multi-round:** Tối đa 10 vòng tool calling (S3) hoặc 5 vòng (C3)
- ✅ **Session history:** Ring buffer 24 turns, tự động ghi đè tin nhắn cũ
- ✅ **Graceful error:** Rollback session nếu LLM fail

### 3.3. Tool Registry - 20 Built-in Tools

**File:** `main/tool/tool_registry.c`

| # | Tool Name | Category | Input Schema | Output |
|---|-----------|----------|--------------|--------|
| 1 | `gpio_write` | GPIO | `{pin: int, state: int}` | `"OK: GPIO{pin}={state}"` |
| 2 | `gpio_read` | GPIO | `{pin: int}` | `"GPIO{pin}={level}"` |
| 3 | `gpio_read_all` | GPIO | `{}` | JSON array các GPIO |
| 4 | `delay` | GPIO | `{milliseconds: int}` | `"Delayed {ms}ms"` |
| 5 | `memory_set` | Memory | `{key: str, value: str}` | `"Saved"` (key phải `u_*`) |
| 6 | `memory_get` | Memory | `{key: str}` | Giá trị hoặc `"null"` |
| 7 | `memory_delete` | Memory | `{key: str}` | `"Deleted"` |
| 8 | `memory_list` | Memory | `{}` | JSON array các key `u_*` |
| 9 | `get_diagnostics` | System | `{}` | heap, uptime, GPIO pins |
| 10 | `get_version` | System | `{}` | Firmware version |
| 11 | `cron_schedule` | Cron | `{type, action, interval_s?, hour?, minute?, delay_s?}` | Task ID |
| 12 | `cron_list` | Cron | `{}` | JSON array các task |
| 13 | `cron_cancel` | Cron | `{id: int}` | `"Cancelled {id}"` |
| 14 | `cron_cancel_all` | Cron | `{}` | `"All cancelled"` |
| 15 | `get_time` | Time | `{}` | ISO8601 timestamp |
| 16 | `set_timezone` | Time | `{timezone: str}` | `"Timezone set"` |
| 17 | `set_persona` | Persona | `{persona: str}` | `"Persona: {name}"` |
| 18 | `get_persona` | Persona | `{}` | Tên persona hiện tại |
| 19 | `wifi_scan` | Network | `{}` | JSON array các AP |
| 20 | `get_network_info` | Network | `{}` | SSID, RSSI, IP, gateway |

**Tool Dispatch Flow:**

```c
bool tool_registry_dispatch(const char *name, const char *input_json,
                             char *result_buf, size_t result_sz) {
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            return s_tools[i].execute(input_json, result_buf, result_sz);
        }
    }
    snprintf(result_buf, result_sz, "Unknown tool: %s", name);
    return false;
}
```

### 3.4. Channel System - 10 Communication Channels

**File:** `main/channel/channel_registry.c`

```c
typedef struct {
    const char *name;
    esp_err_t (*start)(message_bus_t *bus);
    bool (*is_available)(void);
} channel_ops_t;
```

| Channel | Protocol | Direction | Status | Config |
|---------|----------|-----------|--------|--------|
| **Serial** | UART USB-JTAG | Bidirectional | ✅ Verified | Always on |
| **Telegram** | HTTPS Long Polling | Bidirectional | ✅ Verified | Token + Chat IDs |
| **MQTT** | MQTT over TLS 8883 | Bidirectional | ✅ Verified | HiveMQ Cloud |
| **DingTalk** | HTTPS Webhook | Outbound | 🚧 WIP | Webhook URL + Secret |
| **Discord** | HTTPS Webhook | Outbound | 🚧 WIP | Webhook URL |
| **Slack** | HTTPS Webhook | Outbound | 🚧 WIP | Webhook URL |
| **WeCom** | HTTPS Webhook | Outbound | 🚧 WIP | Webhook URL |
| **Lark** | HTTPS Webhook | Outbound | 🚧 WIP | Webhook URL + Secret |
| **PushPlus** | HTTP POST | Outbound | 🚧 WIP | Token |
| **Bark** | HTTP GET | Outbound | 🚧 WIP | Key |

**MQTT Channel Details:**

**File:** `main/channel/channel_mqtt.c`

```c
// MQTT Topics
espclaw/{device_id}/cmd              // Subscribe: Nhận lệnh từ backend
espclaw/{device_id}/otp              // Subscribe: Nhận OTP pairing
espclaw/{device_id}/login_success    // Subscribe: Nhận xác nhận login
espclaw/{device_id}/response         // Publish: Gửi phản hồi
espclaw/{device_id}/status           // Publish: LWT (online/offline)

// MQTT Config
Broker: e855d1adcb91498097194e25175017dd.s1.eu.hivemq.cloud
Port: 8883 (TLS)
Protocol: MQTT v3.1.1
QoS: 1 (At least once)
Keepalive: 60s
ALPN: mqtt
```

**MQTT Event Handler:**

```c
static void mqtt_event_handler(...) {
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            // Subscribe topics
            esp_mqtt_client_subscribe(client, s_topic_cmd, 1);
            esp_mqtt_client_subscribe(client, s_topic_otp, 1);
            esp_mqtt_client_subscribe(client, s_topic_login_success, 1);
            
            // Publish online status
            esp_mqtt_client_publish(client, s_topic_status, "online", 6, 1, 1);
            
            // Trigger delta sync (device→cloud only)
            if (neuron_link_is_paired()) {
                neuron_link_sync(SYNC_MODE_DELTA, SYNC_DIR_DEVICE_TO_CLOUD, NULL);
            }
            break;
            
        case MQTT_EVENT_DATA:
            // Handle OTP message
            if (strncmp(event->topic, s_topic_otp, event->topic_len) == 0) {
                // Parse OTP code → Display on OLED
                oled_ui_show_message("LOGIN REQUEST", "Code:", otp_code);
            }
            
            // Handle login success
            if (strncmp(event->topic, s_topic_login_success, ...) == 0) {
                // Save tenant_id to NVS
                nvs_mgr_set_str("tenant_id", tenant_id);
                neuron_link_set_tenant_id(tenant_id);
                
                // Trigger delta sync
                neuron_link_sync(SYNC_MODE_DELTA, SYNC_DIR_DEVICE_TO_CLOUD, NULL);
            }
            
            // Handle system commands
            if (strstr(payload, "\"action\":\"sync\"")) {
                neuron_link_sync(SYNC_MODE_DELTA, SYNC_DIR_DEVICE_TO_CLOUD, NULL);
            }
            if (strstr(payload, "\"action\":\"pull_config\"")) {
                neuron_link_sync_config_only();  // Cloud→Device (selective)
            }
            
            // Forward to agent
            message_bus_post_inbound(s_bus, &msg, pdMS_TO_TICKS(60000));
            break;
    }
}
```

### 3.5. Neuron Link - Cloud Sync Protocol

**File:** `main/net/neuron_link.c`

```c
typedef enum {
    SYNC_MODE_DELTA,   // Chỉ thay đổi từ lần sync cuối
    SYNC_MODE_FULL,    // Toàn bộ graph
    SYNC_MODE_FORCE    // Bỏ qua last_sync, force full
} sync_mode_t;

typedef enum {
    SYNC_DIR_DEVICE_TO_CLOUD,    // Push local → cloud
    SYNC_DIR_CLOUD_TO_DEVICE,    // Pull cloud → local
    SYNC_DIR_BIDIRECTIONAL        // Both directions
} sync_direction_t;
```

**Sync Flow:**

```c
esp_err_t neuron_link_sync(sync_mode_t mode, sync_direction_t direction, ...) {
    // 1. Prepare local config nodes (Telegram, LLM, GPIO)
    neuron_link_prepare_local_config();
    
    // 2. Device → Cloud (Push unsynced items)
    if (direction == SYNC_DIR_DEVICE_TO_CLOUD || BIDIRECTIONAL) {
        // Push nodes
        cache_node_t **nodes = neuron_cache_get_unsynced_nodes(&count);
        for (i = 0; i < count; i++) {
            cJSON *json = build_node_json(nodes[i]);
            http_post("/rest/v1/nodes", json, ...);
            neuron_cache_mark_node_synced(nodes[i]->id);
        }
        
        // Push links
        cache_link_t **links = neuron_cache_get_unsynced_links(&count);
        // ... similar
        
        // Push pulses
        cache_pulse_t **pulses = neuron_cache_get_unsynced_pulses(&count);
        // ... similar
    }
    
    // 3. Cloud → Device (Pull changes)
    if (direction == SYNC_DIR_CLOUD_TO_DEVICE || BIDIRECTIONAL) {
        uint64_t since = (mode == DELTA) ? neuron_cache_get_last_sync() : 0;
        
        // Pull nodes
        char *nodes_json = neuron_link_pull_nodes(since);
        cJSON *nodes = cJSON_Parse(nodes_json);
        cJSON_ArrayForEach(node, nodes) {
            // Apply config if it's a config node
            neuron_link_apply_config(node);
            
            // Upsert to cache
            neuron_cache_upsert_node(&cn);
        }
        
        // Pull links
        // ... similar
    }
    
    // 4. Pull workflows from cloud
    neuron_link_sync_workflows();
    
    // 5. Update last sync timestamp
    neuron_cache_set_last_sync(time(NULL));
    
    return ESP_OK;
}
```

**Config Sync (Selective):**

```c
esp_err_t neuron_link_sync_config_only(void) {
    // Pull ONLY config nodes from cloud
    // Apply to NVS ONLY if local NVS is empty
    // Preserves existing local config
    
    // 1. Telegram config
    if (node.subtype == "interface_telegram") {
        if (local_token_is_empty()) {
            nvs_mgr_set_str(NVS_KEY_TG_TOKEN, cloud_token);
            esp_restart();  // Reboot to apply
        }
    }
    
    // 2. LLM config
    if (node.subtype == "llm_config") {
        if (local_llm_key_is_empty()) {
            nvs_mgr_set_str(NVS_KEY_LLM_API_KEY, cloud_key);
        }
    }
    
    // 3. GPIO config
    if (node.subtype == "gpio_config") {
        if (local_gpio_is_empty()) {
            nvs_mgr_set_i32("gpio_sda", cloud_sda);
            esp_restart();
        }
    }
}
```

**Quan trọng:**
- ✅ **Default sync:** Device→Cloud only (preserves local config)
- ✅ **Selective pull:** Cloud→Device chỉ khi user yêu cầu
- ✅ **Config protection:** Không overwrite local config đã có
- ✅ **Auto-restart:** Reboot sau khi apply config từ cloud

### 3.6. Memory Management

**Platform-dependent Buffer Allocation:**

```c
// config.h
#if ESPCLAW_HAS_PSRAM
  #define LLM_REQUEST_BUF_SIZE     24576   // 24KB PSRAM
  #define LLM_RESPONSE_BUF_SIZE    24576
  #define MAX_HISTORY_TURNS        24
  #define MAX_TOOL_ROUNDS          10
#else
  #define LLM_REQUEST_BUF_SIZE     8192    // 8KB SRAM
  #define LLM_RESPONSE_BUF_SIZE    8192
  #define MAX_HISTORY_TURNS        8
  #define MAX_TOOL_ROUNDS          5
#endif

// platform.h
#if ESPCLAW_HAS_PSRAM
  #define ESPCLAW_MALLOC(sz)  heap_caps_malloc(sz, MALLOC_CAP_SPIRAM)
  #define ESPCLAW_FREE(ptr)   heap_caps_free(ptr)
#else
  #define ESPCLAW_MALLOC(sz)  malloc(sz)
  #define ESPCLAW_FREE(ptr)   free(ptr)
#endif
```

**FreeRTOS Task Stack Sizes:**

| Task | Stack (S3) | Stack (C3) | Core | Priority |
|------|------------|------------|------|----------|
| agent_task | 8KB | 8KB | Core 1 | 5 |
| serial_input_task | 6KB | 4KB | Core 0 | 5 |
| tg_poll_task | 6KB | 8KB | Core 0 | 5 |
| mqtt_publish_task | 6KB | 6KB | Core 0 | 5 |
| cron_task | 6KB | 4KB | Core 0 | 4 |

---

## 4. Backend & Web Dashboard

### 4.1. Tech Stack

```
Frontend:
├── Next.js 14 (App Router)
├── React 18
├── TypeScript
├── TailwindCSS
├── react-force-graph-3d (Three.js)
└── Supabase Client

Backend:
├── Next.js API Routes (Edge Functions)
├── Supabase PostgreSQL
├── Supabase Realtime (WebSocket)
├── Upstash Redis (Rate Limiting)
└── HiveMQ Cloud (MQTT Broker)
```

### 4.2. Database Schema (Supabase)

**Core Tables:**

```sql
-- devices: ESP32 devices
CREATE TABLE devices (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id UUID NOT NULL REFERENCES tenants(id),
  device_id TEXT UNIQUE NOT NULL,      -- "GETAI-XXXXX"
  mac_address TEXT,
  device_name TEXT,
  device_type TEXT,                     -- "esp32s3", "esp32c3"
  firmware_version TEXT,
  pair_code TEXT,                       -- OTP 6 digits
  pair_expires_at TIMESTAMPTZ,
  is_paired BOOLEAN DEFAULT FALSE,
  is_online BOOLEAN DEFAULT FALSE,
  last_seen_at TIMESTAMPTZ,
  created_at TIMESTAMPTZ DEFAULT NOW()
);

-- nodes: Knowledge graph nodes
CREATE TABLE nodes (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id UUID NOT NULL REFERENCES tenants(id),
  device_id TEXT,
  type TEXT NOT NULL,                   -- user, device, skill, memory, tag...
  subtype TEXT,                         -- interface_telegram, llm_config, gpio_config...
  name TEXT NOT NULL,
  description TEXT,
  pos_x FLOAT DEFAULT 50,               -- 3D position (0-100)
  pos_y FLOAT DEFAULT 50,
  pos_z FLOAT DEFAULT 50,
  is_active BOOLEAN DEFAULT TRUE,
  is_pinned BOOLEAN DEFAULT FALSE,
  content JSONB DEFAULT '{}',           -- Arbitrary data
  activation_count INT DEFAULT 0,
  pulse_strength FLOAT DEFAULT 0,       -- 0.0 - 1.0
  link_count INT DEFAULT 0,
  created_at TIMESTAMPTZ DEFAULT NOW(),
  updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- links: Connections between nodes
CREATE TABLE links (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id UUID NOT NULL REFERENCES tenants(id),
  source_node_id UUID REFERENCES nodes(id) ON DELETE CASCADE,
  target_node_id UUID REFERENCES nodes(id) ON DELETE CASCADE,
  type TEXT NOT NULL,                   -- owns, part_of, triggers, related_to...
  weight FLOAT DEFAULT 0.5,             -- 0.0 - 1.0
  confidence FLOAT DEFAULT 1.0,
  is_active BOOLEAN DEFAULT TRUE,
  last_pulsed_at TIMESTAMPTZ,
  created_at TIMESTAMPTZ DEFAULT NOW(),
  updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- pulses: Neural pulses (activity events)
CREATE TABLE pulses (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id UUID NOT NULL REFERENCES tenants(id),
  device_id TEXT NOT NULL,
  source_node_id UUID REFERENCES nodes(id),
  target_node_id UUID REFERENCES nodes(id),
  type TEXT NOT NULL,                   -- action, perception, insight...
  energy FLOAT DEFAULT 0.5,
  text TEXT,
  metadata JSONB DEFAULT '{}',
  created_at TIMESTAMPTZ DEFAULT NOW()
);

-- workflows: Automation workflows
CREATE TABLE workflows (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id UUID NOT NULL REFERENCES tenants(id),
  name TEXT NOT NULL,
  icon TEXT DEFAULT '⚡',
  category TEXT DEFAULT 'general',
  description TEXT,
  trigger_type TEXT NOT NULL,           -- voice_command, scheduled, event...
  trigger_pattern TEXT,                 -- "bật đèn", "check wifi"...
  trigger_node_id UUID REFERENCES nodes(id),
  steps JSONB NOT NULL,                 -- Array of workflow steps
  is_enabled BOOLEAN DEFAULT TRUE,
  priority INT DEFAULT 50,
  trigger_count INT DEFAULT 0,
  success_count INT DEFAULT 0,
  version INT DEFAULT 1,
  created_at TIMESTAMPTZ DEFAULT NOW(),
  updated_at TIMESTAMPTZ DEFAULT NOW()
);
```

**RLS Policies:**

```sql
-- Tenant isolation
ALTER TABLE devices ENABLE ROW LEVEL SECURITY;
ALTER TABLE nodes ENABLE ROW LEVEL SECURITY;
ALTER TABLE links ENABLE ROW LEVEL SECURITY;
ALTER TABLE pulses ENABLE ROW LEVEL SECURITY;
ALTER TABLE workflows ENABLE ROW LEVEL SECURITY;

CREATE POLICY "Tenant isolation for devices" ON devices
  FOR ALL USING (tenant_id = current_setting('app.tenant_id')::UUID);

CREATE POLICY "Tenant isolation for nodes" ON nodes
  FOR ALL USING (tenant_id = current_setting('app.tenant_id')::UUID);
-- ... similar for other tables
```

### 4.3. API Routes

**Device Pairing Flow:**

```typescript
// POST /api/device/request-otp
{
  device_id: "GETAI-XXXXX",
  tenant_id?: "uuid"  // Optional
}
→ Generate 6-digit OTP
→ Save to devices table (expires in 5 min)
→ Publish MQTT: espclaw/{device_id}/otp
→ Response: { success: true, expires_in_seconds: 300 }

// POST /api/device/verify-otp
{
  device_id: "GETAI-XXXXX",
  otp: "123456",
  tenant_id: "uuid"
}
→ Verify OTP from database
→ Mark device as paired
→ Publish MQTT: espclaw/{device_id}/login_success
→ Response: { success: true, device: {...} }
```

**Workflow API:**

```typescript
// GET /api/workflows?tenant_id=uuid
→ List all workflows for tenant
→ Response: { workflows: [...], total: 10 }

// POST /api/workflows
{
  tenant_id: "uuid",
  name: "LED Blink",
  icon: "💡",
  category: "automation",
  trigger_type: "voice_command",
  trigger_pattern: "bật đèn",
  steps: [
    { id: 0, type: "tool", tool: "gpio_write", params: { pin: 2, state: 1 } },
    { id: 1, type: "wait", milliseconds: 500 },
    { id: 2, type: "tool", tool: "gpio_write", params: { pin: 2, state: 0 } }
  ],
  is_enabled: true,
  priority: 50
}
→ Create workflow + linked node
→ Response: { workflow: {...} }

// POST /api/workflows/{id}/execute
{
  tenant_id: "uuid",
  dry_run: true
}
→ Execute workflow steps
→ Response: { success: true, results: [...], duration_ms: 123 }
```

**Graph API:**

```typescript
// GET /api/graph/workflow-tree?tenant_id=uuid&type=tree
→ Get 3D graph data (nodes + links)
→ Response: { nodes: [...], links: [...] }

// POST /api/neuron/pulse
{
  tenant_id: "uuid",
  device_id: "GETAI-XXXXX",
  source_node_id: "uuid",
  target_node_id: "uuid",
  type: "action",
  energy: 0.8,
  text: "GPIO write executed"
}
→ Create pulse record
→ Trigger PostgreSQL function to update link.last_pulsed_at
→ Broadcast via Supabase Realtime
```

### 4.4. Web Dashboard

**File:** `neutron-web/app/dashboard/page.tsx`

**Features:**

1. **3D Knowledge Graph** (react-force-graph-3d)
   - Nodes: Workflows, Skills, Devices, Memory
   - Links: Triggers, Owns, Related
   - Particles: Animated pulses
   - Interactive: Click node → Open workflow editor

2. **Workflow Editor**
   - Visual step builder
   - Tool picker (20 ESP32 tools)
   - Parameter editor
   - Test execution
   - Enable/Disable toggle

3. **Debug Console**
   - Execution logs
   - Pulse stream
   - Real-time updates (Supabase Realtime)

4. **Settings Modal**
   - Telegram config
   - LLM config
   - GPIO config
   - Sync to ESP32

**Key Components:**

```tsx
// Workflow Editor
function WorkflowEditor({ workflow, isNew, onSave, onClose, ... }) {
  const [steps, setSteps] = useState<WorkflowStep[]>([]);
  
  function addStep(type: 'tool' | 'wait') {
    setSteps([...steps, { id: steps.length, type, ... }]);
  }
  
  async function handleSave() {
    await onSave({ ...form, steps });
  }
  
  return (
    <div>
      {/* Basic Info */}
      <input value={form.name} onChange={...} />
      
      {/* Trigger */}
      <input value={form.trigger_pattern} placeholder="bật đèn" />
      
      {/* Steps */}
      {steps.map((step, idx) => (
        <div key={idx}>
          {step.type === 'tool' ? (
            <select value={step.tool} onChange={...}>
              {ESP32_TOOLS.map(t => <option>{t.name}</option>)}
            </select>
          ) : (
            <input type="number" value={step.milliseconds} />
          )}
        </div>
      ))}
      
      {/* Actions */}
      <button onClick={handleSave}>Save</button>
      <button onClick={() => onTest(workflow.id)}>Test</button>
    </div>
  );
}

// 3D Graph
<ForceGraph3D
  graphData={{ nodes, links }}
  nodeThreeObject={(node) => {
    const size = 6 + (node.pulse_strength || 0) * 10;
    const color = getNodeColor(node.type);
    return new THREE.Sprite(...);
  }}
  onNodeClick={(node) => {
    const wf = workflows.find(w => w.id === node.id);
    if (wf) setSelectedWorkflow(wf);
  }}
  linkDirectionalParticles={2}
  linkDirectionalParticleSpeed={0.005}
/>
```

---

## 5. Luồng Tương Tác Device ↔ Backend

### 5.1. Device Pairing Flow

```
┌─────────────┐                  ┌─────────────┐                  ┌─────────────┐
│   ESP32     │                  │   Backend   │                  │   MQTT      │
│   Device    │                  │   API       │                  │   Broker    │
└──────┬──────┘                  └──────┬──────┘                  └──────┬──────┘
       │                                │                                │
       │ 1. Boot → Generate Device ID  │                                │
       │    "GETAI-XXXXX"               │                                │
       │                                │                                │
       │ 2. Connect MQTT                │                                │
       │────────────────────────────────┼───────────────────────────────>│
       │                                │                                │
       │ 3. Subscribe topics            │                                │
       │    espclaw/GETAI-XXXXX/otp    │                                │
       │    espclaw/GETAI-XXXXX/login_success                           │
       │                                │                                │
       │                                │ 4. User opens Web Dashboard    │
       │                                │    Enter Device ID             │
       │                                │                                │
       │                                │ 5. POST /api/device/request-otp│
       │                                │    { device_id: "GETAI-XXXXX" }│
       │                                │                                │
       │                                │ 6. Generate OTP "123456"       │
       │                                │    Save to DB (5 min TTL)      │
       │                                │                                │
       │                                │ 7. Publish MQTT                │
       │                                │────────────────────────────────>│
       │                                │    espclaw/GETAI-XXXXX/otp     │
       │                                │    {"otp": "123456"}           │
       │                                │                                │
       │ 8. Receive OTP via MQTT        │                                │
       │<───────────────────────────────┼────────────────────────────────│
       │                                │                                │
       │ 9. Display OTP on OLED         │                                │
       │    "LOGIN REQUEST"             │                                │
       │    "Code: 123456"              │                                │
       │                                │                                │
       │                                │ 10. User enters OTP on Web     │
       │                                │                                │
       │                                │ 11. POST /api/device/verify-otp│
       │                                │     { device_id, otp, tenant_id}│
       │                                │                                │
       │                                │ 12. Verify OTP → Mark paired   │
       │                                │                                │
       │                                │ 13. Publish MQTT               │
       │                                │────────────────────────────────>│
       │                                │    espclaw/GETAI-XXXXX/login_success│
       │                                │    {"status": "success",       │
       │                                │     "tenant_id": "uuid"}       │
       │                                │                                │
       │ 14. Receive login_success      │                                │
       │<───────────────────────────────┼────────────────────────────────│
       │                                │                                │
       │ 15. Save tenant_id to NVS      │                                │
       │     Update Neuron Link config  │                                │
       │                                │                                │
       │ 16. Trigger Delta Sync         │                                │
       │     (Device→Cloud)             │                                │
       │     Push local config to cloud │                                │
       │                                │                                │
       │ 17. OLED: "READY"              │                                │
       │                                │                                │
```

### 5.2. Message Flow (User → ESP32 → LLM → Response)

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   User      │     │   MQTT      │     │   ESP32     │     │   LLM API   │
│  (Telegram) │     │   Broker    │     │   Device    │     │  (OpenAI)   │
└──────┬──────┘     └──────┬──────┘     └──────┬──────┘     └──────┬──────┘
       │                   │                   │                   │
       │ 1. Send message   │                   │                   │
       │   "bật đèn"       │                   │                   │
       │──────────────────>│                   │                   │
       │                   │                   │                   │
       │                   │ 2. Publish MQTT   │                   │
       │                   │   espclaw/{id}/cmd│                   │
       │                   │──────────────────>│                   │
       │                   │                   │                   │
       │                   │                   │ 3. Receive → Inbound Queue
       │                   │                   │                   │
       │                   │                   │ 4. Agent Task     │
       │                   │                   │    - Rate limit   │
       │                   │                   │    - Workflow match?
       │                   │                   │      YES → Execute workflow
       │                   │                   │      NO  → Continue
       │                   │                   │                   │
       │                   │                   │ 5. Build prompt   │
       │                   │                   │    + 20 tools     │
       │                   │                   │    + session      │
       │                   │                   │                   │
       │                   │                   │ 6. HTTPS POST     │
       │                   │                   │────────────────────>│
       │                   │                   │   /v1/chat/completions
       │                   │                   │                   │
       │                   │                   │ 7. LLM Response   │
       │                   │                   │<────────────────────│
       │                   │                   │   tool_use: gpio_write
       │                   │                   │                   │
       │                   │                   │ 8. Dispatch tool  │
       │                   │                   │    gpio_write(2, 1)
       │                   │                   │    → "OK: GPIO2=1"│
       │                   │                   │                   │
       │                   │                   │ 9. Append result  │
       │                   │                   │    → Loop (round 2)
       │                   │                   │                   │
       │                   │                   │ 10. HTTPS POST    │
       │                   │                   │────────────────────>│
       │                   │                   │                   │
       │                   │                   │ 11. LLM Response  │
       │                   │                   │<────────────────────│
       │                   │                   │   "Đã bật đèn"    │
       │                   │                   │                   │
       │                   │                   │ 12. Outbound Queue│
       │                   │                   │                   │
       │                   │ 13. Publish MQTT  │                   │
       │                   │<──────────────────│                   │
       │                   │   espclaw/{id}/response               │
       │                   │   "Đã bật đèn"    │                   │
       │                   │                   │                   │
       │ 14. Receive       │                   │                   │
       │<──────────────────│                   │                   │
       │   "Đã bật đèn"    │                   │                   │
       │                   │                   │                   │
```

### 5.3. Cloud Sync Flow

```
┌─────────────┐                  ┌─────────────┐                  ┌─────────────┐
│   ESP32     │                  │   Supabase  │                  │   Web       │
│   Device    │                  │   Database  │                  │   Dashboard │
└──────┬──────┘                  └──────┬──────┘                  └──────┬──────┘
       │                                │                                │
       │ 1. MQTT Connected              │                                │
       │    + Device is paired          │                                │
       │                                │                                │
       │ 2. Trigger Delta Sync          │                                │
       │    (Device→Cloud only)         │                                │
       │                                │                                │
       │ 3. Prepare local config        │                                │
       │    - Telegram token/chat_id    │                                │
       │    - LLM provider/key          │                                │
       │    - GPIO pins (SDA/SCL)       │                                │
       │    → Create cache nodes        │                                │
       │                                │                                │
       │ 4. Get unsynced nodes          │                                │
       │    from NVS cache              │                                │
       │                                │                                │
       │ 5. POST /rest/v1/nodes         │                                │
       │────────────────────────────────>│                                │
       │    [{ id, tenant_id, type,     │                                │
       │       subtype, name, content }]│                                │
       │                                │                                │
       │ 6. Upsert nodes                │                                │
       │                                │ 7. Realtime broadcast          │
       │                                │───────────────────────────────>│
       │                                │    postgres_changes: nodes     │
       │                                │                                │
       │                                │ 8. Update 3D graph             │
       │                                │                                │
       │ 9. Mark nodes as synced        │                                │
       │    in NVS cache                │                                │
       │                                │                                │
       │ 10. POST /rest/v1/links        │                                │
       │────────────────────────────────>│                                │
       │                                │                                │
       │ 11. POST /rest/v1/pulses       │                                │
       │────────────────────────────────>│                                │
       │                                │                                │
       │ 12. GET /rest/v1/workflows     │                                │
       │     ?tenant_id=xxx&is_enabled=true                              │
       │────────────────────────────────>│                                │
       │                                │                                │
       │ 13. Load workflows into        │                                │
       │     workflow engine            │                                │
       │                                │                                │
       │ 14. Update last_sync timestamp │                                │
       │     in NVS                     │                                │
       │                                │                                │
```

**Selective Config Pull (User-triggered):**

```
┌─────────────┐                  ┌─────────────┐                  ┌─────────────┐
│   Web       │                  │   MQTT      │                  │   ESP32     │
│   Dashboard │                  │   Broker    │                  │   Device    │
└──────┬──────┘                  └──────┬──────┘                  └──────┬──────┘
       │                                │                                │
       │ 1. User clicks                 │                                │
       │    "Pull from Cloud" button    │                                │
       │                                │                                │
       │ 2. POST /api/device/config-pull│                                │
       │    { deviceId: "GETAI-XXXXX" } │                                │
       │                                │                                │
       │ 3. Publish MQTT                │                                │
       │────────────────────────────────>│                                │
       │    espclaw/{id}/cmd            │                                │
       │    {"action": "pull_config"}   │                                │
       │                                │                                │
       │                                │ 4. Receive command             │
       │                                │───────────────────────────────>│
       │                                │                                │
       │                                │ 5. neuron_link_sync_config_only()
       │                                │                                │
       │                                │ 6. GET /rest/v1/nodes          │
       │                                │    ?tenant_id=xxx              │
       │                                │    &subtype=in.(interface_telegram,
       │                                │                 llm_config,    │
       │                                │                 gpio_config)   │
       │                                │                                │
       │                                │ 7. For each config node:       │
       │                                │    IF local NVS is empty       │
       │                                │       THEN apply cloud config  │
       │                                │       ELSE skip (preserve local)
       │                                │                                │
       │                                │ 8. If config changed:          │
       │                                │    esp_restart()               │
       │                                │                                │
```

---

## 6. Công Nghệ & Stack

### 6.1. Firmware (ESP32)

| Component | Technology | Version |
|-----------|------------|---------|
| Framework | ESP-IDF | 5.5 |
| RTOS | FreeRTOS | 10.5 |
| Language | Pure C | C11 |
| TLS | mbedTLS | 3.x |
| JSON | cJSON | 1.7.x |
| HTTP Client | esp_http_client | Built-in |
| MQTT Client | esp_mqtt | Built-in |
| NVS | esp_nvs | Built-in |

### 6.2. Backend

| Component | Technology | Version |
|-----------|------------|---------|
| Framework | Next.js | 14 |
| Runtime | Node.js | 20+ |
| Language | TypeScript | 5.x |
| Database | PostgreSQL (Supabase) | 15 |
| Vector DB | pgvector | 0.5.x |
| Realtime | Supabase Realtime | WebSocket |
| Cache | Upstash Redis | Cloud |
| MQTT | HiveMQ Cloud | TLS 8883 |

### 6.3. Frontend

| Component | Technology | Version |
|-----------|------------|---------|
| UI Framework | React | 18 |
| Styling | TailwindCSS | 3.x |
| 3D Graph | react-force-graph-3d | 1.29.x |
| 3D Engine | Three.js | r150+ |
| Physics | d3-force-3d | 3.x |
| Validation | Zod | 3.x |

---

## 7. Các Tính Năng Chính

### 7.1. ReAct Agent

- ✅ **Multi-round tool calling** (max 10 rounds)
- ✅ **Session history** (ring buffer 24 turns)
- ✅ **Workflow-first routing** (skip LLM if workflow matches)
- ✅ **Graceful error handling** (rollback on failure)
- ✅ **Rate limiting** (100/hour, 1000/day)

### 7.2. Tool System

- ✅ **20 built-in tools** (GPIO, Memory, Cron, Network, System, Persona)
- ✅ **JSON schema validation**
- ✅ **Safety guardrails** (GPIO pin range, memory key prefix)
- ✅ **Extensible** (X-macro registration)

### 7.3. Channel System

- ✅ **10 channels** (Serial, Telegram, MQTT, Discord, Slack...)
- ✅ **Bidirectional** (Serial, Telegram, MQTT)
- ✅ **Conditional compilation** (enable/disable per channel)
- ✅ **VTable architecture** (channel_ops_t)

### 7.4. Cloud Sync

- ✅ **Delta sync** (incremental updates)
- ✅ **Full sync** (complete graph)
- ✅ **Selective config pull** (preserves local config)
- ✅ **Workflow sync** (cloud → device)
- ✅ **Realtime updates** (Supabase Realtime)

### 7.5. Workflow Engine

- ✅ **Visual editor** (Web dashboard)
- ✅ **Voice command trigger** (pattern matching)
- ✅ **Step types** (tool, wait, llm, condition...)
- ✅ **Priority-based execution**
- ✅ **Dry-run testing**

### 7.6. 3D Knowledge Graph

- ✅ **Interactive visualization** (Three.js)
- ✅ **Node types** (user, device, skill, memory, tag...)
- ✅ **Link types** (owns, triggers, related_to...)
- ✅ **Animated pulses** (activity visualization)
- ✅ **Real-time updates** (WebSocket)

---

## 8. Bảo Mật & Tối Ưu

### 8.1. Security

- ✅ **Row Level Security (RLS)** - Tenant isolation
- ✅ **TLS/SSL** - All HTTPS/MQTTS connections
- ✅ **API Key encryption** - Stored in NVS
- ✅ **OTP pairing** - 6-digit code, 5-min expiry
- ✅ **Rate limiting** - Upstash Redis + NVS counters
- ✅ **GPIO guardrails** - Pin range + allowlist

### 8.2. Memory Optimization

- ✅ **Platform-dependent buffers** (PSRAM vs SRAM)
- ✅ **Static allocation** (avoid malloc in loops)
- ✅ **Ring buffer** (session history)
- ✅ **Streaming parser** (cJSON on C3)
- ✅ **Task stack tuning** (per-platform)

### 8.3. Performance

- ✅ **Dual-core** (Agent on Core 1, Channels on Core 0)
- ✅ **FreeRTOS queues** (non-blocking message passing)
- ✅ **TLS mutex** (prevent OOM on concurrent HTTPS)
- ✅ **Exponential backoff** (Telegram polling)
- ✅ **Watchdog feeding** (vTaskDelay in loops)

### 8.4. Reliability

- ✅ **Graceful degradation** (LLM fail → error message)
- ✅ **Auto-reconnect** (WiFi, MQTT)
- ✅ **NVS persistence** (config, session, cron)
- ✅ **Factory reset** (GPIO button hold 5s)
- ✅ **OTA updates** (GitHub Releases)

---

## 📊 Tổng Kết

ESPClaw là một dự án **production-ready** với kiến trúc **modular**, **scalable** và **maintainable**. Firmware được viết hoàn toàn bằng **Pure C** với **8,478 dòng code**, tích hợp **ReAct Agent**, **20 tools**, **10 channels**, và **cloud sync** với Supabase.

**Điểm mạnh:**
- ✅ Chạy trên vi điều khiển giá rẻ ($2-5)
- ✅ Không phụ thuộc thư viện AI bên ngoài
- ✅ Multi-LLM support (OpenAI, Anthropic, Ollama, Custom)
- ✅ Workflow engine (tự động hóa không cần LLM)
- ✅ 3D Knowledge Graph (visualization)
- ✅ Cloud sync (Supabase + MQTT)
- ✅ Production-ready (RLS, rate limiting, error handling)

**Use Cases:**
- 🏠 Smart Home Automation
- 🤖 IoT AI Assistant
- 📊 Data Logging & Monitoring
- 🔧 Hardware Prototyping
- 🎓 Educational Projects

---

**Generated by:** AI Technical Analysis  
**Date:** 6/5/2026  
**Version:** 1.0.0
