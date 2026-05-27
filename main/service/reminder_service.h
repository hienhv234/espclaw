/*
 * ESPClaw - service/reminder_service.h
 *
 * Polls backend for due reminders and queues notifications to the agent loop.
 *
 * Requirements:
 *   - cron_init() must be called first (uses cron_is_time_synced())
 *   - backend_url must be set in NVS (key: NVS_KEY_BACKEND_URL)
 *   - tenant_id must be set in NVS (key: NVS_KEY_TENANT_ID)
 *   - Optionally device_id in NVS (key: NVS_KEY_DEVICE_ID)
 */

#ifndef REMINDER_SERVICE_H
#define REMINDER_SERVICE_H

#include "esp_err.h"
#include "freertos/queue.h"
#include <stdbool.h>

/**
 * Initialize reminder service (reads config from NVS).
 * Safe to call multiple times.
 */
esp_err_t reminder_service_init(void);

/**
 * Start the reminder polling background task.
 *
 * @param agent_input_queue  Queue to send inbound_msg_t notifications to.
 *                            This is typically the same queue passed to agent_start().
 * @return ESP_OK on success
 */
esp_err_t reminder_service_start(QueueHandle_t agent_input_queue);

/**
 * Enable or disable reminder polling.
 */
void reminder_service_set_enabled(bool enabled);

/**
 * Check if reminder service is enabled.
 */
bool reminder_service_is_enabled(void);

/**
 * Refresh config from NVS (call after backend_url/tenant_id changes).
 */
void reminder_service_refresh_config(void);

#endif /* REMINDER_SERVICE_H */
