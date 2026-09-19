#include "battery.h"
#include "ina219.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#define BATTERY_TASK_STACK_SIZE 3072
#define BATTERY_TASK_PRIORITY 2
#define BATTERY_SAMPLE_PERIOD_MS 100
#define BATTERY_CALCULATION_PERIOD_MS 1000

static const char *TAG = "battery";

static battery_data_t s_battery_data;
static float s_energy_since_fetch_wh;
static esp_err_t s_battery_status = ESP_ERR_NOT_FOUND;
static portMUX_TYPE s_battery_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_battery_task;
static bool s_battery_initialized;

static void battery_task(void *arg)
{
  (void)arg;

  float voltage_sum = 0.0f;
  float current_sum = 0.0f;
  float power_sum = 0.0f;
  uint32_t sample_count = 0;
  float calculation_energy_wh = 0.0f;
  float previous_power_w = 0.0f;
  int64_t previous_sample_timestamp_us = 0;
  bool previous_sample_valid = false;
  int64_t calculation_start_timestamp_us = esp_timer_get_time();
  TickType_t wake_time = xTaskGetTickCount();

  while (true) {
    ina219_measurement_t measurement;
    const esp_err_t ret = ina219_read_measurement(&measurement);
    const int64_t timestamp_us = esp_timer_get_time();

    if (ret == ESP_OK) {
      voltage_sum += measurement.voltage;
      current_sum += measurement.current;
      power_sum += measurement.power;
      sample_count++;

      if (previous_sample_valid && timestamp_us > previous_sample_timestamp_us) {
        const float elapsed_hours = (float)(timestamp_us - previous_sample_timestamp_us) / 3600000000.0f;
        calculation_energy_wh += ((previous_power_w + measurement.power) * 0.5f) * elapsed_hours;
      }

      previous_power_w = measurement.power;
      previous_sample_timestamp_us = timestamp_us;
      previous_sample_valid = true;
    } else {
      previous_sample_valid = false;
    }

    if (timestamp_us - calculation_start_timestamp_us >= BATTERY_CALCULATION_PERIOD_MS * 1000LL) {
      portENTER_CRITICAL(&s_battery_lock);

      memset(&s_battery_data, 0, sizeof(s_battery_data));
      s_battery_data.timestamp = (uint32_t)(timestamp_us / 1000);
      s_battery_data.energy = 0.0f;
      s_battery_status = sample_count > 0 ? ESP_OK : ret;

      if (sample_count > 0) {
        s_battery_data.voltage = voltage_sum / (float)sample_count;
        s_battery_data.current = current_sum / (float)sample_count;
        s_battery_data.power = power_sum / (float)sample_count;
        s_battery_data.energy = calculation_energy_wh;
        s_battery_data.valid = true;
        s_energy_since_fetch_wh += calculation_energy_wh;
      }

      portEXIT_CRITICAL(&s_battery_lock);

      voltage_sum = 0.0f;
      current_sum = 0.0f;
      power_sum = 0.0f;
      sample_count = 0;
      calculation_energy_wh = 0.0f;
      calculation_start_timestamp_us = timestamp_us;
    }

    vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(BATTERY_SAMPLE_PERIOD_MS));
  }
}

void battery_init(void)
{
  if (s_battery_initialized) {
    return;
  }

  ina219_init();

  portENTER_CRITICAL(&s_battery_lock);
  memset(&s_battery_data, 0, sizeof(s_battery_data));
  s_energy_since_fetch_wh = 0.0f;
  s_battery_status = ESP_ERR_NOT_FOUND;
  portEXIT_CRITICAL(&s_battery_lock);

  if (xTaskCreate(battery_task, "battery_task", BATTERY_TASK_STACK_SIZE, NULL, BATTERY_TASK_PRIORITY,
                  &s_battery_task) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create battery task");
    return;
  }

  s_battery_initialized = true;
  ESP_LOGI(TAG, "Battery management initialized");
}

esp_err_t battery_fetch_data(battery_data_t *data)
{
  if (data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  portENTER_CRITICAL(&s_battery_lock);
  *data = s_battery_data;
  data->energy = s_energy_since_fetch_wh;
  const esp_err_t ret = s_battery_status;
  s_energy_since_fetch_wh = 0.0f;
  portEXIT_CRITICAL(&s_battery_lock);

  return ret;
}
