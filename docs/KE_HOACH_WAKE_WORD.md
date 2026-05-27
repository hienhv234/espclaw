# Kế hoạch xây dựng chức năng dạy AI Wake Word (ESP32 Mic)

> **Theo dõi tiến độ:** [KE_HOACH_WAKE_WORD_CHECKLIST.md](./KE_HOACH_WAKE_WORD_CHECKLIST.md)

> **Dự án:** ESPClaw  
> **Phiên bản tài liệu:** 1.0  
> **Ngày:** 25/05/2026  
> **Phạm vi:** ESP32-S3 + PSRAM (ưu tiên), tích hợp Web `neutron-web` + Firmware Pure C

---

## Mục lục

1. [Tóm tắt điều hành](#1-tóm-tắt-điều-hành)
2. [Hiện trạng codebase](#2-hiện-trạng-codebase)
3. [Định nghĩa chức năng](#3-định-nghĩa-chức-năng)
4. [Kiến trúc đề xuất](#4-kiến-trúc-đề-xuất)
5. [So sánh công nghệ](#5-so-sánh-công-nghệ)
6. [Lộ trình triển khai](#6-lộ-trình-triển-khai)
7. [Đánh giá tác động (Impact)](#7-đánh-giá-tác-động-impact)
8. [Rủi ro và giảm thiểu](#8-rủi-ro-và-giảm-thiểu)
9. [Quyết định kiến trúc](#9-quyết-định-kiến-trúc)
10. [Phụ lục](#10-phụ-lục)

---

## 1. Tóm tắt điều hành

ESPClaw cần bổ sung khả năng **wake word tùy chỉnh** — người dùng dạy thiết bị một cụm từ kích hoạt (ví dụ: *"Ê Claw"*, *"Xin chào Neutron"*) bằng cách thu âm qua **mic I2S** trên ESP32, sau đó thiết bị lắng nghe liên tục và kích hoạt **ReAct Agent** khi nhận diện đúng.

**Hướng triển khai được đề xuất:** mô hình **lai (hybrid)**:

| Tầng | Vai trò |
|------|---------|
| **Firmware** | Thu âm mẫu, VAD, suy luận wake word cục bộ, đẩy sự kiện vào `message_bus` |
| **Web (neutron-web)** | Giao diện hướng dẫn thu mẫu, quản lý dataset, kích hoạt huấn luyện |
| **Cloud / Edge Impulse** | Huấn luyện model KWS (Keyword Spotting), OTA model về thiết bị |

**Phần cứng mục tiêu đầu tiên:** **Seeed XIAO ESP32S3 Plus** (`./build.sh xiao_s3_plus`) — 16MB Flash, 8MB PSRAM, **OLED SSD1306** I2C (SDA=GPIO5, SCL=GPIO6).  
**Không khuyến nghị** triển khai wake word ML trên **ESP32-C3/C5** (không PSRAM, heap hạn chế).

---

## 2. Hiện trạng codebase

### 2.1. Đã có sẵn

| Thành phần | Trạng thái | Vị trí / Ghi chú |
|-----------|------------|------------------|
| Cấu hình GPIO mic I2S | ✅ Có | `main/Kconfig.projbuild` — `ESPCLAW_MIC_I2S_WS/SCK/SD` |
| Lưu GPIO mic vào NVS | ✅ Có | `main/net/wifi_ap.c`, `neuron_link.c` |
| Thu / phát WAV, phổ âm | ✅ Có | `components/lua_modules/lua_module_audio/` — `esp_codec_dev` |
| Lua API ghi âm | ✅ Có | `audio.record_wav()`, `audio.mic_read_level()` |
| Board manager audio | ✅ Có | `lua_module_board_manager` — `get_audio_codec_input()` |
| ReAct Agent + message bus | ✅ Có | `main/agent/agent_loop.c` |
| LittleFS (PSRAM target) | ✅ Có | `partitions_16mb.csv` — ~12MB cho file/model |
| Workflow `voice_command` | ⚠️ Một phần | Chỉ **khớp văn bản/regex**, không phải nhận diện giọng nói thực |

### 2.2. Chưa có

- Module **I2S capture** dùng chung trong `main/` (Pure C, không phụ thuộc Lua)
- Tích hợp **esp-sr** / WakeNet / TFLite Micro
- Task FreeRTOS **lắng nghe wake word** liên tục
- API Web + MQTT cho **huấn luyện & OTA model**
- Tool Agent: `wakeword_train_*`, `wakeword_enable`

### 2.3. Macro nền tảng liên quan

```c
// main/platform.h — wake word chỉ bật khi:
#if ESPCLAW_TARGET_S3 && ESPCLAW_HAS_PSRAM
  #define ESPCLAW_HAS_WAKEWORD 1
#else
  #define ESPCLAW_HAS_WAKEWORD 0
#endif
```

*(Macro `ESPCLAW_HAS_WAKEWORD` cần được thêm khi triển khai — hiện chưa tồn tại.)*

---

## 3. Định nghĩa chức năng

### 3.1. Hai lớp chức năng

#### Lớp 1 — Dạy (Training)

1. Người dùng vào chế độ huấn luyện (Web, Serial, hoặc Agent hướng dẫn).
2. Thu **mẫu dương** (nói wake word) — khuyến nghị **15–30 lần**, mỗi đoạn 1–2 giây.
3. Thu **mẫu âm** (tiếng ồn, câu khác, im lặng) — khuyến nghị **30+ lần**.
4. Upload qua MQTT/HTTPS lên backend.
5. Cloud huấn luyện model KWS → trả file model (`.tflite` / `.espdl` / binary esp-sr).
6. OTA hoặc ghi vào LittleFS → kích hoạt model mới.

#### Lớp 2 — Suy luận (Inference)

1. Task nền đọc PCM 16 kHz mono từ I2S (khung 20–30 ms).
2. VAD lọc im lặng → giảm CPU.
3. Engine wake word chấm điểm → vượt ngưỡng → phát sự kiện `WAKE_DETECTED`.
4. `agent_loop` nhận sự kiện → bắt đầu phiên ReAct (tương đương user gửi tin nhắn).
5. (Tùy chọn) Tiếp theo: ASR → text → `workflow_engine` trigger `WF_TRIGGER_VOICE`.

### 3.2. Luồng người dùng (UX)

```
[Bình thường] Mic lắng nghe (LED/icon "Đang nghe")
      ↓ phát hiện wake word
[Đã kích hoạt] Hiển thị TFT "Đã gọi" → Agent xử lý
      ↓ hoàn tất / timeout
[Quay lại] Cooldown 2–3 giây (chống kích hoạt liên tiếp)
```

### 3.3. Chế độ huấn luyện (UX)

```
Web: "Nhấn Ghi" → User nói wake word → Lặp 20 lần
     → "Ghi tiếng ồn xung quanh" → 30 lần
     → "Huấn luyện" → Đợi 1–3 phút → "Tải model xuống thiết bị"
```

---

## 4. Kiến trúc đề xuất

### 4.1. Sơ đồ tổng quan

```
┌─────────────────────────────────────────────────────────────────┐
│                        ESP32-S3 (PSRAM)                          │
│  ┌──────────┐   ┌─────────┐   ┌──────────────┐   ┌────────────┐ │
│  │ I2S Mic  │──▶│   VAD   │──▶│ Wake Engine  │──▶│ message_bus│ │
│  └──────────┘   └─────────┘   │ (vtable)     │   └─────┬──────┘ │
│        ▲                      └──────────────┘         │        │
│        │ Capture (train)                               ▼        │
│  ┌──────────┐                                   ┌────────────┐  │
│  │ Sample   │                                   │ agent_loop │  │
│  │ Recorder │                                   │ (Core 1)   │  │
│  └────┬─────┘                                   └────────────┘  │
└───────┼─────────────────────────────────────────────────────────┘
        │ MQTT / HTTPS (mẫu WAV, model OTA)
        ▼
┌───────────────────┐     ┌────────────────────┐
│  neutron-web API  │────▶│ Edge Impulse / KWS │
│  /api/wakeword/*  │     │ Huấn luyện model   │
└───────────────────┘     └────────────────────┘
```

### 4.2. Module firmware mới (Pure C)

| File / Thư mục | Trách nhiệm |
|----------------|-------------|
| `main/audio/i2s_capture.c` | Khởi tạo I2S, ring buffer PCM, đọc khung cố định |
| `main/audio/audio_vad.c` | VAD nhẹ (WebRTC VAD hoặc AFE esp-sr) |
| `main/wakeword/wakeword_ops.h` | VTable: `init`, `feed`, `set_model`, `get_score`, `deinit` |
| `main/wakeword/wakeword_esp_sr.c` | Adapter ESP-SR WakeNet (MVP cố định) |
| `main/wakeword/wakeword_tflite.c` | TFLite Micro — model tùy chỉnh (Phase 3) |
| `main/wakeword/wakeword_task.c` | FreeRTOS task, ghim Core 0 |
| `main/wakeword/wakeword_train.c` | State machine thu mẫu + upload |
| `main/tool/tool_wakeword.c` | Tool Agent: train / enable / status |

### 4.3. VTable (theo convention ESPClaw)

```c
typedef struct {
    esp_err_t (*init)(const wakeword_config_t *cfg);
    esp_err_t (*feed)(const int16_t *pcm, size_t samples);
    float     (*get_score)(void);
    esp_err_t (*set_model)(const char *path);
    esp_err_t (*set_threshold)(float t);
    void      (*deinit)(void);
} wakeword_ops_t;
```

### 4.4. Tích hợp message bus

Thêm loại tin nhắn mới (ví dụ):

```c
typedef enum {
    ...
    BUS_MSG_WAKE_DETECTED,
    BUS_MSG_WAKE_TRAIN_PROGRESS,
} bus_msg_type_t;
```

`wakeword_task` → `bus_publish(BUS_MSG_WAKE_DETECTED, session_id, NULL)` → `agent_loop` xử lý như input từ channel ảo `wake`.

### 4.5. Web API (neutron-web)

| Endpoint | Phương thức | Mô tả |
|----------|-------------|-------|
| `/api/wakeword/upload` | POST | Nhận mẫu WAV (Zod validate, RLS `tenant_id`) |
| `/api/wakeword/train` | POST | Kích hoạt huấn luyện trên Edge Impulse |
| `/api/wakeword/model/[deviceId]` | GET | Tải metadata + URL model |
| `/api/wakeword/status/[deviceId]` | GET | Trạng thái train / version model trên thiết bị |

### 4.6. MQTT Topics

| Topic | Hướng | Payload |
|-------|-------|---------|
| `espclaw/{id}/wakeword/cmd` | Cloud → Device | `{"action":"train_start"}` |
| `espclaw/{id}/wakeword/samples` | Device → Cloud | Chunk WAV base64 |
| `espclaw/{id}/wakeword/model` | Cloud → Device | `{"url":"...","version":2,"sha256":"..."}` |
| `espclaw/{id}/wakeword/event` | Device → Cloud | `{"event":"detected","score":0.92}` |

---

## 5. So sánh công nghệ

### 5.1. Bảng đánh giá

| Phương án | Wake word tùy chỉnh | RAM ước tính | Flash model | Độ chính xác | Độ khó tích hợp | Khuyến nghị |
|----------|---------------------|--------------|-------------|--------------|-----------------|-------------|
| **A. ESP-SR (WakeNet9)** | Chỉ từ có sẵn của Espressif; custom qua toolchain riêng | ~300 KB | 200–400 KB | Cao (từ cố định) | Trung bình | **MVP Phase 1** |
| **B. Edge Impulse KWS** | Hoàn toàn tùy chỉnh | 50–200 KB | 50–300 KB | Trung cao (phụ thuộc mẫu) | Cao | **Chính cho "dạy AI"** |
| **C. microWakeWord (TFLite)** | Tùy chỉnh | 30–80 KB | 30–100 KB | Trung bình | Trung bình | Dự phòng nhẹ |
| **D. MFCC + DTW template** | Ghi 1 mẫu làm mẫu | < 20 KB | < 10 KB | Thấp, nhiễu cao | Thấp | Chỉ PoC |
| **E. Porcupine (Picovoice)** | Console web | ~100 KB | Nhúng sẵn | Cao | Thấp | Không phù hợp chi phí $2 |
| **F. Cloud ASR liên tục** | Bất kỳ từ nào | Gần 0 ML | 0 | Cao | Thấp | Trái mục tiêu offline |

### 5.2. Chi tiết phương án A — ESP-SR

**Ưu điểm:**

- Chính thức từ Espressif, tối ưu cho ESP32-S3.
- Có sẵn AFE (khử echo, VAD).
- Tài liệu IDF 5.x phong phú.

**Nhược điểm:**

- Wake word tùy chỉnh cần quy trình train của Espressif (không linh hoạt như Edge Impulse).
- Model tiếng Việt hạn chế hơn tiếng Trung/Anh.

**Dùng khi:** Cần chứng minh pipeline **nghe → kích hoạt Agent** trong 2–3 tuần.

### 5.3. Chi tiết phương án B — Edge Impulse (đề xuất chính)

**Ưu điểm:**

- UX "dạy AI" rõ ràng: thu mẫu trên ESP → train trên cloud → OTA.
- Export TFLite / EON Compiler cho ESP32.
- Hỗ trợ đánh giá FAR/FRR trên dashboard.

**Nhược điểm:**

- Phụ thuộc dịch vụ bên thứ ba (hoặc self-host phức tạp hơn).
- Cần backend API trung gian.

**Dùng khi:** Triển khai sản phẩm **wake word do người dùng đặt tên**.

### 5.4. Thông số âm thanh chuẩn

| Tham số | Giá trị |
|---------|---------|
| Sample rate | 16 kHz |
| Bit depth | 16-bit signed PCM |
| Channels | 1 (mono) |
| Frame size | 512 samples (32 ms @ 16 kHz) |
| Định dạng mẫu train | WAV, 1–2 giây/mẫu |

---

## 6. Lộ trình triển khai

### Phase 0 — Hạ tầng audio (1–2 tuần)

- [ ] Tạo `main/audio/i2s_capture.c` — tách logic từ `lua_module_audio`
- [ ] Ring buffer tĩnh (không `malloc` trong vòng lặp đọc mic)
- [ ] Thêm `ESPCLAW_HAS_WAKEWORD` trong `platform.h` + Kconfig
- [ ] Kiểm tra compile trên `sdkconfig.defaults.esp32s3_tft_n16r8`

**Tiêu chí hoàn thành:** Đọc mic 10 giây, log RMS/peak qua Serial.

---

### Phase 1 — Wake word cố định MVP (2–3 tuần)

- [ ] Thêm dependency `esp-sr` (`idf_component.yml`)
- [ ] Implement `wakeword_esp_sr.c` + `wakeword_task.c` (Core 0)
- [ ] Kết nối `message_bus` → `agent_loop`
- [ ] Hiển thị trạng thái trên TFT/OLED
- [ ] Kconfig bật/tắt wake word; lưu ngưỡng vào NVS

**Tiêu chí hoàn thành:**

- Từ cố định (ví dụ *"Hi ESP"*) kích hoạt Agent trong phòng yên tĩnh, khoảng cách 2–3 m.
- FAR (false accept rate) < 5% trong 1 giờ nghe thử.

---

### Phase 2 — Thu mẫu & luồng "dạy" (3–4 tuần)

**Firmware:**

- [ ] `wakeword_train.c` — state machine: `IDLE → POS → NEG → UPLOAD`
- [ ] Lưu mẫu tạm: `/littlefs/wakeword/samples/`
- [ ] Upload MQTT chunked + SHA256
- [ ] Tool Agent: `wakeword_train_start`, `wakeword_train_record`, `wakeword_train_finish`

**Web:**

- [ ] Trang `/dashboard/devices/[id]/wakeword`
- [ ] API Zod + RLS Supabase
- [ ] Tích hợp Edge Impulse API (hoặc queue huấn luyện nội bộ)

**Tiêu chí hoàn thành:** User thu 20 mẫu trên Web + ESP, train xong, model version 1 chạy trên thiết bị.

---

### Phase 3 — Model tùy chỉnh on-device (2–3 tuần)

- [ ] `wakeword_tflite.c` — TFLite Micro inference
- [ ] OTA model qua MQTT URL hoặc HTTP proxy
- [ ] NVS: `wakeword_model_ver`, `wakeword_threshold`
- [ ] Rollback model nếu FRR tăng đột biến

**Tiêu chí hoàn thành:** Wake word do user đặt (tiếng Việt 2–4 âm tiết) hoạt động ổn định sau train đủ mẫu.

---

### Phase 4 — Trải nghiệm & tối ưu (liên tục)

- [ ] VAD giảm CPU khi im lặng
- [ ] Cooldown chống double-trigger
- [ ] Nối `WF_TRIGGER_VOICE` với ASR (tùy chọn)
- [ ] Báo cáo metric: FAR/FRR lên Supabase
- [ ] Chế độ tiết kiệm: tắt listen khi không có WiFi / pin yếu

---

### Ước lượng effort

| Giai đoạn | Thời gian (1 kỹ sư ESP-IDF) |
|-----------|----------------------------|
| Phase 0–1 | 3–4 tuần |
| Phase 2–3 | 6–8 tuần |
| Phase 4 + QA | 2–4 tuần |
| **Tổng** | **~3–4 tháng** đến bản release "dạy wake word" |

---

## 7. Đánh giá tác động (Impact)

### 7.1. Sản phẩm & trải nghiệm người dùng

| Khía cạnh | Mức độ | Mô tả |
|-----------|--------|-------|
| Khác biệt hóa | **Cao** | Chuyển từ "gõ lệnh / MQTT" sang "gọi tên thiết bị" — phù hợp IoT giá rẻ |
| Trường hợp sử dụng | **Cao** | Nhà bếp, xưởng, tay đang bận — kết hợp 20 hardware tools |
| Độ phức tạp UX | **Trung bình** | Cần hướng dẫn thu mẫu rõ ràng; nếu không đủ mẫu → FRR cao |
| Hỗ trợ đa ngôn ngữ | **Trung bình** | Tiếng Việt cần nhiều mẫu hơn; có thể cần data augmentation trên cloud |

### 7.2. Kỹ thuật & hệ thống

| Tài nguyên | Tác động ước tính | Ghi chú |
|------------|-------------------|---------|
| **RAM (PSRAM)** | +200–400 KB khi listen | Tránh chạy đồng thời peak LLM + wake inference |
| **Flash app** | +100–200 KB (esp-sr / TFLite runtime) | Kiểm tra `ota_0` 2MB có đủ sau tích hợp |
| **Flash model** | 50–500 KB | Nên partition `model` riêng (1–2 MB) trên bản 16MB |
| **CPU Core 0** | ~15–25% liên tục | Bắt buộc `vTaskDelay`, tránh TWDT |
| **Điện năng** | Tăng đáng kể vs deep sleep | Cần toggle "Bật wake word" trong cài đặt |
| **WiFi** | Chỉ khi upload/train | Inference **offline hoàn toàn** |
| **Heap fragmentation** | Rủi ro nếu malloc trong loop | **Bắt buộc** buffer tĩnh theo `.cursorrules` |

### 7.3. Tương tác module hiện có

```
wakeword_detected
    → message_bus (MSG_WAKE)
    → agent_loop (có thể bypass ratelimit lần đầu — cần thiết kế)
    → workflow_engine (WF_TRIGGER_VOICE sau ASR)
    → neuron_link (đồng bộ node cấu hình wake word)
    → channel MQTT (báo dashboard realtime)
```

### 7.4. Phạm vi phần cứng

| Chip | Wake word ML | Ghi chú |
|------|--------------|---------|
| ESP32-S3 + PSRAM | ✅ Hỗ trợ đầy đủ | **XIAO S3 Plus** (OLED), XIAO S3 Sense |
| ESP32-S3 không PSRAM | ⚠️ Hạn chế | Chỉ wake word cố định nhẹ, không train |
| ESP32-C3 / C5 | ❌ Không khuyến nghị | Ghi rõ "không hỗ trợ" trong UI |

### 7.5. Bảo mật

| Rủi ro | Biện pháp |
|--------|-----------|
| OTA model giả mạo | Kiểm tra chữ ký SHA256 + TLS |
| Mẫu giọng nói lộ | Mã hóa HTTPS; RLS tenant; xóa mẫu sau train |
| Kích hoạt Agent trái phép | Cooldown + ngưỡng score + tùy chọn xác nhận âm thanh |

---

## 8. Rủi ro và giảm thiểu

| # | Rủi ro | Xác suất | Ảnh hưởng | Giảm thiểu |
|---|--------|----------|-----------|------------|
| 1 | False wake làm phiền user | Cao | Cao | VAD + threshold + cooldown 3s |
| 2 | Mẫu train không đủ | Cao | Cao | Validate tối thiểu 15 pos / 30 neg trên API |
| 3 | Xung đột GPIO I2S / SPI TFT | Trung bình | Cao | Bảng pin theo board; test trên `esp32s3_tft_n16r8` |
| 4 | OOM khi Agent + Wake cùng lúc | Trung bình | Cao | Mutex: tạm dừng wake khi Agent đang gọi LLM |
| 5 | Tiếng Việt nhận diện kém | Trung bình | Trung bình | Augmentation; hướng dẫn phát âm rõ; nhiều speaker |
| 6 | Model OTA fail giữa chừng | Thấp | Cao | Dual slot model + rollback version cũ |
| 7 | TWDT reset task listen | Trung bình | Trung bình | `vTaskDelay(1)` mỗi frame; watchdog feed |

---

## 9. Quyết định kiến trúc

| # | Quyết định | Lý do |
|---|------------|-------|
| 1 | Chỉ bật `ESPCLAW_HAS_WAKEWORD` trên **S3 + PSRAM** | Đủ RAM cho ML; khớp LittleFS hiện có |
| 2 | MVP dùng **ESP-SR cố định** trước | Giảm rủi ro; validate pipeline Agent |
| 3 | "Dạy AI" dùng **Edge Impulse** (hoặc KWS self-host) | UX train + export TFLite chuẩn |
| 4 | VTable `wakeword_ops_t` | Đồng bộ kiến trúc channel/provider |
| 5 | Không malloc trong `wakeword_task` loop | Tránh phân mảnh heap (quy tắc dự án) |
| 6 | Partition `model` riêng (dài hạn) | OTA model không đụng LittleFS user data |
| 7 | C3/C5: hiển thị "Không hỗ trợ" | Tránh kỳ vọng sai trên thiết bị $2 |

---

## 10. Phụ lục

### 10.1. OLED I2C (XIAO S3 Plus)

| Tín hiệu | GPIO |
|----------|------|
| SDA | 5 (D4) |
| SCL | 6 (D5) |
| Địa chỉ I2C | 0x3C |

### 10.2. GPIO mic (mặc định sdkconfig)

| Tín hiệu | GPIO mặc định |
|----------|---------------|
| MIC SCK | 41 |
| MIC WS | 42 |
| MIC SD | 40 |

*(Có thể ghi đè qua NVS / trang cấu hình WiFi AP.)*

### 10.3. Khóa NVS đề xuất

| Khóa | Kiểu | Mô tả |
|------|------|-------|
| `wakeword_enabled` | u8 | 0/1 bật listen |
| `wakeword_threshold` | float | Ngưỡng 0.0–1.0 |
| `wakeword_model_ver` | u32 | Phiên bản model hiện tại |
| `wakeword_model_path` | string | Đường dẫn file trong LittleFS |

### 10.4. Tool Agent đề xuất

| Tool | Tham số | Mô tả |
|------|---------|-------|
| `wakeword_enable` | `enabled: bool` | Bật/tắt listen |
| `wakeword_status` | — | Trả score, version, trạng thái train |
| `wakeword_train_start` | `label: string` | Bắt đầu session train |
| `wakeword_train_record` | `type: "pos"\|"neg"` | Ghi 1 mẫu |
| `wakeword_train_finish` | — | Upload và yêu cầu train |

### 10.5. Tài liệu tham khảo

- [ESP-SR Documentation](https://docs.espressif.com/projects/esp-sr/en/latest/)
- [Edge Impulse — Keyword Spotting](https://docs.edgeimpulse.com/docs/keyword-spotting)
- ESPClaw: `components/lua_modules/lua_module_audio/skills/lua_module_audio.md`
- ESPClaw: `main/platform.h`, `partitions_16mb.csv`
- ESPClaw: `sdkconfig.defaults.xiao_esp32s3_plus`

### 10.6. Bước tiếp theo (khi bắt đầu code)

1. Tạo `docs/` (file này) ✅  
2. Scaffold `main/audio/` + `main/wakeword/`  
3. Thêm `ESPCLAW_HAS_WAKEWORD` vào `platform.h`  
4. Draft JSON schema MQTT trong `neutron-web/lib/validation.ts`  
5. PR Phase 0: chỉ đọc mic + log RMS — **không** tích hợp ML

---

*Tài liệu này là kế hoạch kỹ thuật nội bộ. Cập nhật khi hoàn thành từng Phase.*
