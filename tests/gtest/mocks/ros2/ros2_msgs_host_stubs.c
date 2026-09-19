#include "ros2_msgs_host_stubs.h"
#include "usb_bridge.h"
#include <assert.h>
#include <setjmp.h>
#include <string.h>

typedef struct {
  void (*entry)(void *);
  void *arg;
  const char *name;
  unsigned int notifications;
} host_task_t;

typedef struct {
  size_t item_size;
  bool pending;
  uint8_t data[sizeof(navigation_imu_sample_t)];
} host_queue_t;

ros2_host_faults_t ros2_host_faults;

static host_task_t tasks[2];
static host_queue_t queues[3];
static size_t task_count, queue_count;
static void (*timer_callback)(TimerHandle_t);
static navigation_telemetry_callback_t imu_callback;
static usb_bridge_cb_t rx_callback;
static bool timer_enabled;
static TickType_t timer_period;
static uint8_t rx_data[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
static size_t rx_length;
static uint64_t last_set_time;
static jmp_buf task_exit;
static bool task_running, task_waited;

void ros2_host_reset(void)
{
  memset(&ros2_host_faults, 0, sizeof(ros2_host_faults));
  ros2_host_faults.battery_valid = true;
  ros2_host_faults.snapshot_valid = true;
  ros2_host_faults.imu_valid = true;
  ros2_host_faults.motor_result = true;
  memset(tasks, 0, sizeof(tasks));
  memset(queues, 0, sizeof(queues));
  task_count = queue_count = rx_length = 0;
  timer_callback = NULL;
  imu_callback = NULL;
  rx_callback = NULL;
  timer_enabled = task_running = task_waited = false;
  timer_period = 0;
  last_set_time = 0;
}

bool ros2_host_set_time(uint64_t unix_seconds)
{
  if (ros2_host_faults.set_time_fails)
    return false;
  last_set_time = unix_seconds;
  return true;
}

uint64_t ros2_host_last_set_time(void) { return last_set_time; }

uint64_t ros2_host_get_uptime_ms(void) { return 1234; }

static host_task_t *find_task(const char *name)
{
  for (size_t i = 0; i < task_count; ++i)
    if (strcmp(tasks[i].name, name) == 0)
      return &tasks[i];
  assert(0 && "Task was not registered");
  return NULL;
}

void ros2_host_run_task(const char *name)
{
  host_task_t *task = find_task(name);
  assert(!task_running);
  task_running = true;
  task_waited = false;
  /* Both setjmp and the task wait are in C. No C++ objects are unwound. */
  if (setjmp(task_exit) == 0)
    task->entry(task->arg);
  task_running = false;
}

unsigned int ros2_host_notifications(const char *name) { return find_task(name)->notifications; }

BaseType_t xTaskCreate(void (*task)(void *), const char *name, uint32_t stack_depth, void *arg, UBaseType_t priority,
                       TaskHandle_t *task_handle)
{
  (void)stack_depth;
  (void)priority;
  assert(task_count < sizeof(tasks) / sizeof(tasks[0]));
  host_task_t *created = &tasks[task_count++];
  created->entry = task;
  created->arg = arg;
  created->name = name;
  if (ros2_host_faults.failed_task_name != NULL && strcmp(name, ros2_host_faults.failed_task_name) == 0) {
    *task_handle = NULL;
    return 0;
  }
  *task_handle = created;
  return pdTRUE;
}

void xTaskNotifyGive(TaskHandle_t task) { ++((host_task_t *)task)->notifications; }

uint32_t ulTaskNotifyTake(BaseType_t clear_count_on_exit, TickType_t ticks_to_wait)
{
  (void)clear_count_on_exit;
  (void)ticks_to_wait;
  assert(task_running);
  if (task_waited)
    longjmp(task_exit, 1);
  task_waited = true;
  return 1;
}

QueueHandle_t xQueueCreate(uint32_t length, uint32_t item_size)
{
  assert(length == 1);
  assert(queue_count < sizeof(queues) / sizeof(queues[0]));
  assert(item_size <= sizeof(queues[0].data));
  host_queue_t *queue = &queues[queue_count++];
  queue->item_size = item_size;
  return queue;
}

BaseType_t xQueueOverwrite(QueueHandle_t handle, const void *item)
{
  host_queue_t *queue = handle;
  memcpy(queue->data, item, queue->item_size);
  queue->pending = true;
  return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t handle, void *item, TickType_t ticks_to_wait)
{
  (void)ticks_to_wait;
  host_queue_t *queue = handle;
  if (!queue->pending)
    return 0;
  memcpy(item, queue->data, queue->item_size);
  queue->pending = false;
  return pdTRUE;
}

TimerHandle_t xTimerCreate(const char *name, TickType_t period, BaseType_t auto_reload, void *timer_id,
                           void (*callback)(TimerHandle_t))
{
  (void)name;
  (void)auto_reload;
  (void)timer_id;
  timer_callback = callback;
  timer_period = period;
  return ros2_host_faults.timer_create_fails ? NULL : &timer_period;
}

BaseType_t xTimerStart(TimerHandle_t timer, TickType_t ticks_to_wait)
{
  (void)timer;
  (void)ticks_to_wait;
  timer_enabled = true;
  return pdTRUE;
}

BaseType_t xTimerStop(TimerHandle_t timer, TickType_t ticks_to_wait)
{
  (void)timer;
  (void)ticks_to_wait;
  timer_enabled = false;
  return pdTRUE;
}

BaseType_t xTimerChangePeriod(TimerHandle_t timer, TickType_t period, TickType_t ticks_to_wait)
{
  (void)timer;
  (void)ticks_to_wait;
  timer_period = period;
  return pdTRUE;
}

bool ros2_host_timer_enabled(void) { return timer_enabled; }

TickType_t ros2_host_timer_period(void) { return timer_period; }

void ros2_host_fire_timer(void)
{
  if (timer_enabled && timer_callback != NULL)
    timer_callback(&timer_period);
}

void navigation_set_telemetry_callback(navigation_telemetry_callback_t callback) { imu_callback = callback; }

void ros2_host_emit_imu(const navigation_imu_sample_t *sample)
{
  assert(imu_callback != NULL);
  imu_callback(sample);
}

void ros2_host_receive(const uint8_t *data, size_t len)
{
  assert(len <= sizeof(rx_data));
  memcpy(rx_data, data, len);
  rx_length = len;
  assert(rx_callback != NULL);
  rx_callback();
}

size_t usb_bridge_write_bytes(uint8_t *data, size_t len)
{
  (void)data;
  return len;
}

size_t usb_bridge_read_bytes(uint8_t *data, size_t len)
{
  const size_t count = len < rx_length ? len : rx_length;
  memcpy(data, rx_data, count);
  rx_length -= count;
  memmove(rx_data, rx_data + count, rx_length);
  return count;
}

void usb_bridge_init(void) {}

bool usb_bridge_is_connected(void) { return true; }

void usb_bridge_set_callback(usb_bridge_cb_t callback) { rx_callback = callback; }

void diag_log_ros2(uint8_t msg_type, uint8_t seq, const uint8_t *payload, size_t payload_len)
{
  (void)msg_type;
  (void)seq;
  (void)payload;
  (void)payload_len;
}

bool motor_set(float left_mps, float right_mps)
{
  (void)left_mps;
  (void)right_mps;
  return ros2_host_faults.motor_result;
}

esp_err_t battery_fetch_data(battery_data_t *data)
{
  if (data == NULL)
    return ESP_FAIL;
  data->valid = ros2_host_faults.battery_valid;
  data->timestamp = 123456789U;
  data->voltage = 12.34f;
  data->current = 1.23f;
  data->power = 15.2f;
  data->energy = 123.4f;
  return ros2_host_faults.battery_result;
}

esp_err_t navigation_get_snapshot(navigation_snapshot_t *snapshot)
{
  if (snapshot == NULL)
    return ESP_FAIL;
  snapshot->imu_valid = ros2_host_faults.snapshot_valid;
  snapshot->imu.timestamp_us = 987654321;
  snapshot->imu.acceleration_mps2[0] = 1.0f;
  snapshot->imu.acceleration_mps2[1] = 2.0f;
  snapshot->imu.acceleration_mps2[2] = 3.0f;
  snapshot->imu.angular_velocity_rad_s[0] = 4.0f;
  snapshot->imu.angular_velocity_rad_s[1] = 5.0f;
  snapshot->imu.angular_velocity_rad_s[2] = 6.0f;
  snapshot->imu.magnetic_field_uT[0] = 7.0f;
  snapshot->imu.magnetic_field_uT[1] = 8.0f;
  snapshot->imu.magnetic_field_uT[2] = 9.0f;
  snapshot->imu.quaternion[0] = 0.1f;
  snapshot->imu.quaternion[1] = 0.2f;
  snapshot->imu.quaternion[2] = 0.3f;
  snapshot->imu.quaternion[3] = 0.4f;
  snapshot->imu.data_valid = ros2_host_faults.imu_valid;
  return ros2_host_faults.navigation_result;
}
