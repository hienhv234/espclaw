# Checklist — Wake Word (dạy AI)

> Cập nhật tiến độ triển khai. Đánh dấu `[x]` khi hoàn thành.  
> Kế hoạch chi tiết: [KE_HOACH_WAKE_WORD.md](./KE_HOACH_WAKE_WORD.md)

**Trạng thái tổng:** Phase 0–1 đang triển khai (stub engine + I2S)

---

## Phase 0 — Hạ tầng audio

| # | Hạng mục | Trạng thái |
|---|----------|------------|
| 0.1 | Macro `ESPCLAW_HAS_WAKEWORD` trong `platform.h` | [x] |
| 0.2 | Kconfig `CONFIG_ESPCLAW_WAKEWORD_ENABLE` (chỉ S3) | [x] |
| 0.3 | Module `main/audio/i2s_capture.c` (I2S std, buffer tĩnh) | [x] |
| 0.4 | Đọc GPIO mic từ Kconfig + NVS | [x] |
| 0.5 | `CMakeLists.txt` + `INCLUDE_DIRS audio wakeword` | [x] |
| 0.6 | Bật mặc định trên `sdkconfig.defaults.xiao_esp32s3_plus` (OLED) | [x] |
| 0.7 | Build `idf.py build` thành công | [ ] |

---

## Phase 1 — Wake word MVP (stub → Agent)

| # | Hạng mục | Trạng thái |
|---|----------|------------|
| 1.1 | `wakeword_ops.h` + engine stub (RMS/VAD đơn giản) | [x] |
| 1.2 | `wakeword_task.c` (Core 0, cooldown, pause khi Agent bận) | [x] |
| 1.3 | `MSG_SOURCE_WAKE` trong `messages.h` | [x] |
| 1.4 | `wakeword_post_to_agent()` → `message_bus` | [x] |
| 1.5 | Serial channel in/out cho `MSG_SOURCE_WAKE` | [x] |
| 1.6 | Khởi động trong `main.c` | [x] |
| 1.7 | NVS keys: enabled, threshold, model_ver | [x] |
| 1.8 | Tools: `wakeword_enable`, `wakeword_status` | [x] |
| 1.9 | Tích hợp **ESP-SR WakeNet** (engine thật) | [ ] |
| 1.10 | Hiển thị trạng thái trên OLED (`LISTEN` / `WAKE`) | [x] |
| 1.11 | Đo FAR/FRR phòng yên tĩnh 1 giờ | [ ] |

---

## Phase 2 — Thu mẫu & dạy (Web + MQTT)

| # | Hạng mục | Trạng thái |
|---|----------|------------|
| 2.1 | `wakeword_train.c` state machine | [x] skeleton |
| 2.2 | Tools: `wakeword_train_start/record/finish` | [x] skeleton |
| 2.3 | Lưu mẫu WAV vào LittleFS | [ ] |
| 2.4 | MQTT upload samples | [x] train MQTT cmds |
| 2.5 | API `/api/wakeword/apply` (3 mode) | [x] |
| 2.6 | UI 3 tab: library / keyword / train | [x] |
| 2.7 | **ESP mic test** — thu âm tu ESP → MQTT PCM chunks → WAV → frontend | [x] |
| 2.8 | `POST /api/wakeword/mic-test` — collect audio + serve WAV | [x] |
| 2.9 | `audio/mic_test.c` — I2S capture task → MQTT `espclaw/{id}/audio` | [x] |
| 2.10 | UI tab train: +mau duong/+mau am tu ESP mic (thay browser mic) | [x] |
| 2.11 | Nut "Test Mic ESP" — phat lai am tu ESP tren frontend | [x] |
| 2.12 | Edge Impulse train pipeline | [ ] |

---

## Phase 3 — Model tùy chỉnh on-device

| # | Hạng mục | Trạng thái |
|---|----------|------------|
| 3.1 | `wakeword_engine_tflite.c` | [ ] |
| 3.2 | OTA model qua MQTT/HTTP | [ ] |
| 3.3 | Rollback model version | [ ] |
| 3.4 | Partition `model` riêng (16MB flash) | [ ] |

---

## Phase 4 — Tối ưu & sản phẩm

| # | Hạng mục | Trạng thái |
|---|----------|------------|
| 4.1 | WebRTC VAD / ESP-SR AFE | [ ] |
| 4.2 | Mutex pause listen khi LLM đang chạy | [x] cơ bản |
| 4.3 | Metric FAR/FRR → Supabase | [ ] |
| 4.4 | `WF_TRIGGER_VOICE` + ASR | [ ] |
| 4.5 | Tài liệu người dùng (tiếng Việt) | [ ] |

---

## Ghi chú triển khai hiện tại

- **Board mặc định:** `./build.sh xiao_s3_plus` — OLED SSD1306 I2C (SDA=5, SCL=6), 16MB Flash + PSRAM.
- **Engine stub:** phát hiện theo năng lượng RMS (âm lớn liên tục) — dùng để test pipeline, **không** thay wake word thật.
- **Audio tu ESP mic:** MQTT PCM binary (16 kHz mono 16-bit) → backend ghep WAV → frontend phat lai / dung lam mau train.
- Backend: `POST /api/wakeword/mic-test` collect tu `espclaw/{id}/audio`, tra ve WAV. File tu dong xoa sau 60s.
- Bật/tắt: tool `wakeword_enable` hoặc NVS `wakeword_enabled`.
- Ngưỡng RMS: NVS `wakeword_threshold` (mặc định ~1200, scale int16).
- OLED: `LISTEN` khi bật wake word; `WAKE!` khi kích hoạt Agent.
- UI Wake Word: toan bo bang tieng Viet, mic tu ESP.

---

*Cập nhật lần cuối: 26/05/2026*
