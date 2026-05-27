# ESPClaw main component
# Add sources incrementally as modules are implemented.
#
# Channel sources are conditionally compiled based on Kconfig options.
# Only enabled channels are included to save flash and RAM.

set(SRCS
    "main.c"
    "display_ui.c"
    "mem/nvs_manager.c"
    "mem/neuron_cache.c"
    "net/wifi_manager.c"
    "net/wifi_ap.c"
    "net/neuron_link.c"
    "bus/message_bus.c"
    "util/text_buffer.c"
    "channel/channel_registry.c"
    # LLM provider
    "provider/llm_auth.c"
    "provider/provider_registry.c"
    "provider/provider_anthropic.c"
    "provider/provider_openai.c"
    # Agent loop
    "agent/agent_loop.c"
    # ReAct agent modules
    "agent/session.c"
    "agent/context_builder.c"
    "agent/persona.c"
    # Tool system
    "util/json_util.c"
    "hal/hal_gpio.c"
    "tool/tool_registry.c"
    # Services
    "service/cron_service.c"
    "service/reminder_service.c"
    # Utilities
    "util/ratelimit.c"
    # Workflow engine
    "workflow/workflow_engine.c"
    "tool/tool_wakeword.c"
)

if(CONFIG_ESPCLAW_WAKEWORD_ENABLE)
    list(APPEND SRCS
        "audio/i2s_capture.c"
        "audio/mic_test.c"
        "audio/voice_capture.c"
        "wakeword/wakeword.c"
        "wakeword/wakeword_engine_esp_sr.c"
        "wakeword/wakeword_train.c"
        "provider/provider_gemini_audio.c"
    )
    message(STATUS "ESPClaw: Wake word enabled (stub engine)")
endif()

# =========================================================================
# Conditionally add channel sources based on Kconfig
# =========================================================================

# --- Bidirectional channels ---
if(CONFIG_ESPCLAW_CHANNEL_SERIAL)
    list(APPEND SRCS "channel/channel_serial.c")
    message(STATUS "ESPClaw: Serial channel enabled")
endif()

if(CONFIG_ESPCLAW_CHANNEL_TELEGRAM)
    list(APPEND SRCS "channel/channel_telegram.c")
    list(APPEND SRCS "channel/telegram_helpers.c")
    message(STATUS "ESPClaw: Telegram channel enabled")
endif()

# --- One-way notification channels (webhook-based) ---
if(CONFIG_ESPCLAW_CHANNEL_DINGTALK)
    list(APPEND SRCS "channel/channel_dingtalk.c")
    message(STATUS "ESPClaw: DingTalk channel enabled")
endif()

if(CONFIG_ESPCLAW_CHANNEL_DISCORD)
    list(APPEND SRCS "channel/channel_discord.c")
    message(STATUS "ESPClaw: Discord channel enabled")
endif()

if(CONFIG_ESPCLAW_CHANNEL_SLACK)
    list(APPEND SRCS "channel/channel_slack.c")
    message(STATUS "ESPClaw: Slack channel enabled")
endif()

if(CONFIG_ESPCLAW_CHANNEL_WECOM)
    list(APPEND SRCS "channel/channel_wecom.c")
    message(STATUS "ESPClaw: WeCom channel enabled")
endif()

if(CONFIG_ESPCLAW_CHANNEL_LARK)
    list(APPEND SRCS "channel/channel_lark.c")
    message(STATUS "ESPClaw: Lark/Feishu channel enabled")
endif()

if(CONFIG_ESPCLAW_CHANNEL_PUSHPLUS)
    list(APPEND SRCS "channel/channel_pushplus.c")
    message(STATUS "ESPClaw: Pushplus channel enabled")
endif()

if(CONFIG_ESPCLAW_CHANNEL_BARK)
    list(APPEND SRCS "channel/channel_bark.c")
    message(STATUS "ESPClaw: Bark channel enabled")
endif()

if(CONFIG_ESPCLAW_CHANNEL_MQTT)
    list(APPEND SRCS "channel/channel_mqtt.c")
    message(STATUS "ESPClaw: MQTT channel enabled")
endif()

if(CONFIG_ESPCLAW_DISPLAY_OLED)
    list(APPEND SRCS "oled_ui.c")
    message(STATUS "ESPClaw: OLED display enabled")
endif()

if(CONFIG_ESPCLAW_DISPLAY_TFT)
    list(APPEND SRCS "tft_ui.c")
    message(STATUS "ESPClaw: TFT ILI9341 display enabled")
endif()

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "." "mem" "net" "bus" "provider" "agent" "tool" "service" "util" "hal" "channel" "workflow" "audio" "wakeword" "${CMAKE_CURRENT_SOURCE_DIR}/../components/esp_websocket_client/include"
    REQUIRES
        nvs_flash esp_event esp_timer esp_netif esp_wifi driver
        esp_http_client esp_http_server esp-tls mbedtls
        mqtt json spi_flash mdns esp_lcd esp_lcd_ili9341
        esp_websocket_client app_update led_strip
)
