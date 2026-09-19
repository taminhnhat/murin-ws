#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef unsigned int TickType_t;
typedef void *TaskHandle_t;
typedef void *TimerHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;

#define pdTRUE 1
#define portMAX_DELAY ((TickType_t)~0u)
#define CONFIG_TINYUSB_CDC_RX_BUFSIZE 1024
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_ERR_NO_MEM -2
#define ESP_ERROR_CHECK(expr) (void)(expr)

typedef struct {
  float voltage;
  float current;
  float power;
  float energy;
  uint32_t timestamp;
  bool valid;
} battery_data_t;

typedef struct {
  int64_t timestamp_us;
  float acceleration_mps2[3];
  float angular_velocity_rad_s[3];
  float magnetic_field_uT[3];
  float quaternion[4];
  bool data_valid;
} navigation_imu_sample_t;

typedef struct {
  bool imu_valid;
  navigation_imu_sample_t imu;
} navigation_snapshot_t;

typedef void (*navigation_telemetry_callback_t)(const navigation_imu_sample_t *sample);

void navigation_set_telemetry_callback(navigation_telemetry_callback_t callback);

static inline void motor_get(float *left_mps, float *right_mps)
{
  if (left_mps != NULL)
    *left_mps = 0.25f;
  if (right_mps != NULL)
    *right_mps = -0.5f;
}

esp_err_t battery_fetch_data(battery_data_t *data);

static inline void led_set(uint32_t red, int32_t green, int32_t blue)
{
  (void)red;
  (void)green;
  (void)blue;
}

#define ESP_LOGI(tag, fmt, ...) (void)0
#define ESP_LOGW(tag, fmt, ...) (void)0
#define ESP_LOGD(tag, fmt, ...) (void)0
#define ESP_LOGE(tag, fmt, ...) (void)0

BaseType_t xTaskCreate(void (*task)(void *), const char *name, uint32_t stack_depth, void *arg, UBaseType_t priority,
                       TaskHandle_t *task_handle);
void xTaskNotifyGive(TaskHandle_t task);
QueueHandle_t xQueueCreate(uint32_t length, uint32_t item_size);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks_to_wait);
uint32_t ulTaskNotifyTake(BaseType_t clear_count_on_exit, TickType_t ticks_to_wait);
TimerHandle_t xTimerCreate(const char *name, TickType_t period, BaseType_t auto_reload, void *timer_id,
                           void (*callback)(TimerHandle_t));
BaseType_t xTimerStart(TimerHandle_t timer, TickType_t ticks_to_wait);
BaseType_t xTimerStop(TimerHandle_t timer, TickType_t ticks_to_wait);
BaseType_t xTimerChangePeriod(TimerHandle_t timer, TickType_t period, TickType_t ticks_to_wait);

/* Deterministic host scheduler: run one cycle, returning at the next wait. */
void ros2_host_reset(void);
void ros2_host_run_task(const char *name);
unsigned int ros2_host_notifications(const char *name);
void ros2_host_receive(const uint8_t *data, size_t len);
void ros2_host_fire_timer(void);
void ros2_host_emit_imu(const navigation_imu_sample_t *sample);
bool ros2_host_timer_enabled(void);
TickType_t ros2_host_timer_period(void);
bool ros2_host_set_time(uint64_t unix_seconds);
uint64_t ros2_host_last_set_time(void);
uint64_t ros2_host_get_uptime_ms(void);

esp_err_t navigation_get_snapshot(navigation_snapshot_t *snapshot);

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)1; }

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks_to_wait)
{
  (void)semaphore;
  (void)ticks_to_wait;
  return pdTRUE;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
  (void)semaphore;
  return pdTRUE;
}

/* Reset to successful responses by ros2_host_reset(). */
typedef struct {
  esp_err_t battery_result;
  bool battery_valid;
  esp_err_t navigation_result;
  bool snapshot_valid;
  bool imu_valid;
  bool motor_result;
  bool timer_create_fails;
  bool set_time_fails;
  const char *failed_task_name;
} ros2_host_faults_t;

extern ros2_host_faults_t ros2_host_faults;
