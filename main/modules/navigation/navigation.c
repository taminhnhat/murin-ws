#include "navigation.h"

#include "bno085.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "navigation";

#define NAVIGATION_TASK_STACK_SIZE 4096
#define NAVIGATION_TASK_PRIORITY 5

static navigation_snapshot_t s_snapshot;
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized;
static navigation_monitor_fn_t s_monitor;
static navigation_telemetry_callback_t s_telemetry_callback;

static void navigation_update_task(void *arg)
{
  (void)arg;
  int64_t last_monitor_us = 0;
  int64_t last_error_log_us = 0;
  while (true) {
    navigation_imu_sample_t sample;
    esp_err_t read_ret = bno085_wait_for_data(1000);
    if (read_ret == ESP_OK)
      read_ret = bno085_process_pending(&sample);

    if (read_ret == ESP_OK) {
      taskENTER_CRITICAL(&s_snapshot_lock);
      s_snapshot.imu = sample;
      s_snapshot.imu_valid = sample.data_valid;
      taskEXIT_CRITICAL(&s_snapshot_lock);

      if (s_telemetry_callback != NULL)
        s_telemetry_callback(&sample);

      const int64_t now_us = esp_timer_get_time();
      if (s_monitor != NULL && now_us - last_monitor_us >= 100000) {
        s_monitor(&sample);
        last_monitor_us = now_us;
      }
    } else {
      const int64_t error_now_us = esp_timer_get_time();
      if (error_now_us - last_error_log_us >= 1000000) {
        ESP_LOGW(TAG, "BNO085 read failed: %s", esp_err_to_name(read_ret));
        last_error_log_us = error_now_us;
      }
    }

    const int64_t now_us = esp_timer_get_time();
    if (s_monitor != NULL && now_us - last_monitor_us >= 100000) {
      navigation_imu_sample_t monitor_sample;
      taskENTER_CRITICAL(&s_snapshot_lock);
      monitor_sample = s_snapshot.imu;
      taskEXIT_CRITICAL(&s_snapshot_lock);
      s_monitor(&monitor_sample);
      last_monitor_us = now_us;
    }
  }
}

esp_err_t navigation_init(void)
{
  if (s_initialized) {
    return ESP_OK;
  }

  memset(&s_snapshot, 0, sizeof(s_snapshot));
  esp_err_t ret = bno085_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "BNO085 initialization failed: %s", esp_err_to_name(ret));
    return ret;
  }

  if (xTaskCreate(navigation_update_task, "navigation", NAVIGATION_TASK_STACK_SIZE, NULL, NAVIGATION_TASK_PRIORITY,
                  NULL) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "Navigation initialized");
  return ESP_OK;
}

esp_err_t navigation_get_snapshot(navigation_snapshot_t *snapshot)
{
  if (snapshot == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  taskENTER_CRITICAL(&s_snapshot_lock);
  *snapshot = s_snapshot;
  taskEXIT_CRITICAL(&s_snapshot_lock);
  return snapshot->imu_valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}

void navigation_set_monitor(navigation_monitor_fn_t monitor) { s_monitor = monitor; }

void navigation_set_telemetry_callback(navigation_telemetry_callback_t callback) { s_telemetry_callback = callback; }
