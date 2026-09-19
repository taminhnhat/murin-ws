#include "ros2_msgs.h"

#include <string.h>
#ifdef UNIT_TEST
#include "ros2_msgs_host_stubs.h"
#else
#include "battery.h"
#include "board_led.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "flash_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "navigation.h"
#include "sdkconfig.h"
#include <sys/time.h>
#endif

#include "diag.h"

#if defined(CONFIG_ROS2_TRANSPORT_UART) && CONFIG_ROS2_TRANSPORT_UART
#include "uart_bridge.h"
#else
#include "usb_bridge.h"
#endif

#include "diff_drive.h"

static const char *TAG = "ros2_msgs";

#define ROS2_RUNTIME_SAVE_PERIOD_MS (60 * 1000)

static ros2_msgs_ctx_t ros2_ctx;

static TimerHandle_t telemetry_timer = NULL;

static volatile bool telemetry_enabled = true;
static uint64_t total_runtime_ms = 0;
static uint64_t utc_offset_ms = 0;
#ifndef UNIT_TEST
static int64_t runtime_start_us = 0;
#endif
static uint32_t telemetry_period_ms = 20;
static uint32_t telemetry_mask = UINT32_MAX;
static SemaphoreHandle_t tx_mutex = NULL;
static TaskHandle_t command_task = NULL;
static TaskHandle_t telemetry_task = NULL;
static ros2_msgs_monitor_fn_t ros2_monitor = NULL;
static QueueHandle_t imu_state_tx_queue = NULL;
static QueueHandle_t battery_state_tx_queue = NULL;
static QueueHandle_t drive_state_tx_queue = NULL;
#ifndef UNIT_TEST
static TaskHandle_t runtime_task = NULL;
#endif

static size_t ros2_msgs_read(ros2_msgs_ctx_t *msgs, uint8_t *data, size_t len);
static void ros2_msgs_handle_frame(void *ctx, uint8_t msg_type, uint8_t seq, const uint8_t *payload, size_t len);
static void ros2_msgs_send_imu_state_sample(ros2_msgs_ctx_t *msgs, uint8_t seq, const navigation_imu_sample_t *sample);
static void ros2_msgs_queue_drive_state(void);

static bool ros2_msgs_transport_connected(void)
{
#if defined(CONFIG_ROS2_TRANSPORT_UART) && CONFIG_ROS2_TRANSPORT_UART
  return true;
#else
  return usb_bridge_is_connected();
#endif
}

static bool ros2_msgs_set_utc(uint64_t unix_seconds)
{
  if (unix_seconds > INT64_MAX / 1000)
    return false;
#ifdef UNIT_TEST
  const uint64_t uptime_ms = ros2_host_get_uptime_ms();
#else
  const uint64_t uptime_ms = (uint64_t)(esp_timer_get_time() / 1000);
#endif
  const uint64_t unix_ms = unix_seconds * 1000;
  if (unix_ms < uptime_ms)
    return false;
  const uint64_t offset_ms = unix_ms - uptime_ms;
#ifdef UNIT_TEST
  if (!ros2_host_set_time(unix_seconds))
    return false;
#else
  const struct timeval now = {.tv_sec = (time_t)unix_seconds, .tv_usec = 0};
  if (settimeofday(&now, NULL) != 0 || flash_storage_set_utc_offset_ms(offset_ms) != ESP_OK)
    return false;
#endif
  utc_offset_ms = offset_ms;
  return true;
}

#ifndef UNIT_TEST
static void ros2_msgs_load_settings(void)
{
  telemetry_enabled = flash_storage_get_telemetry_enabled(true);
  total_runtime_ms = flash_storage_get_total_runtime_ms();
  utc_offset_ms = flash_storage_get_utc_offset_ms();
}

static void ros2_msgs_save_runtime(void)
{
  const int64_t now_us = esp_timer_get_time();
  if (now_us >= runtime_start_us)
    total_runtime_ms += (uint64_t)((now_us - runtime_start_us) / 1000);
  runtime_start_us = now_us;

  ESP_ERROR_CHECK(flash_storage_set_total_runtime_ms(total_runtime_ms));
}

static void ros2_runtime_task(void *pvParameters)
{
  (void)pvParameters;
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(ROS2_RUNTIME_SAVE_PERIOD_MS));
    ros2_msgs_save_runtime();
  }
}
#endif

static void telemetry_timer_cb(TimerHandle_t xTimer)
{
  if (telemetry_task != NULL) {
    const uint8_t pending = 1;
    if (battery_state_tx_queue != NULL)
      xQueueOverwrite(battery_state_tx_queue, &pending);
    ros2_msgs_queue_drive_state();
    xTaskNotifyGive(telemetry_task);
  }
}

static void ros2_imu_state_callback(const navigation_imu_sample_t *sample)
{
  if (sample == NULL || telemetry_task == NULL)
    return;

  if (imu_state_tx_queue != NULL)
    xQueueOverwrite(imu_state_tx_queue, sample);
  xTaskNotifyGive(telemetry_task);
}

static void ros2_msgs_queue_drive_state(void)
{
  drive_state_t state = {0};
  motor_get(&state.left_velocity, &state.right_velocity);
  state.timestamp_ms = (uint32_t)ros2_msgs_get_total_runtime_ms();
  state.linear_velocity = (state.left_velocity + state.right_velocity) * 0.5f;
  /* The schema has no wheel-track field, so preserve the signed velocity
   * difference as the available
   * rotational-motion value. */
  state.angular_velocity = state.right_velocity - state.left_velocity;
  if (drive_state_tx_queue != NULL)
    xQueueOverwrite(drive_state_tx_queue, &state);
}

void ros2_msgs_on_rx(void)
{
  if (command_task != NULL) {
    xTaskNotifyGive(command_task);
  }
}

void ros2_msgs_set_monitor(ros2_msgs_monitor_fn_t monitor) { ros2_monitor = monitor; }

void ros2_msgs_set_telemetry_enabled(bool enabled)
{
  telemetry_enabled = enabled;

#ifndef UNIT_TEST
  flash_storage_set_telemetry_enabled(enabled);
#endif

  if (telemetry_timer == NULL)
    return;

  if (enabled)
    xTimerStart(telemetry_timer, 0);
  else
    xTimerStop(telemetry_timer, 0);
}

bool ros2_msgs_get_telemetry_enabled(void) { return telemetry_enabled; }

uint64_t ros2_msgs_get_total_runtime_ms(void)
{
#ifdef UNIT_TEST
  return total_runtime_ms;
#else
  const int64_t now_us = esp_timer_get_time();
  if (now_us <= runtime_start_us)
    return total_runtime_ms;
  return total_runtime_ms + (uint64_t)((now_us - runtime_start_us) / 1000);
#endif
}

uint64_t ros2_msgs_get_utc_offset_ms(void) { return utc_offset_ms; }

void ros2_command_task(void *pvParameters)
{
  (void)pvParameters;
  ros2_msgs_ctx_t *msgs = (ros2_msgs_ctx_t *)pvParameters;
  static uint8_t rx_buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];

  ESP_LOGD(TAG, "ros2 command task start");

  while (1) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

    while (1) {
      const size_t rx_size = ros2_msgs_read(msgs, rx_buf, sizeof(rx_buf));
      if (rx_size == 0) {
        break;
      }

      framed_link_process(&msgs->link, rx_buf, rx_size);
    }
  }
}

void ros2_telemetry_task(void *pvParameters)
{
  (void)pvParameters;
  ros2_msgs_ctx_t *msgs = (ros2_msgs_ctx_t *)pvParameters;
  msgs->tx_seq = 0;

  ESP_LOGD(TAG, "ros2 telemetry task start");

  while (1) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    /* Opening the USB CDC port asserts DTR. Drop queued telemetry while no
     * host is connected instead of
     * repeatedly attempting writes that cannot
     * succeed. Command responses remain unaffected. */
    const bool can_publish = ros2_msgs_transport_connected();

    navigation_imu_sample_t sample;
    while (xQueueReceive(imu_state_tx_queue, &sample, 0) == pdTRUE) {
      if (can_publish && telemetry_enabled && (telemetry_mask & ROS2_TELEM_MASK_IMU_STATE) != 0)
        ros2_msgs_send_imu_state_sample(msgs, msgs->tx_seq++, &sample);
    }

    uint8_t battery_pending;
    while (xQueueReceive(battery_state_tx_queue, &battery_pending, 0) == pdTRUE) {
      if (can_publish && telemetry_enabled && (telemetry_mask & ROS2_TELEM_MASK_BATTERY_STATE) != 0)
        ros2_msgs_send_battery_state(msgs, msgs->tx_seq++);
    }
    drive_state_t drive_state;
    while (xQueueReceive(drive_state_tx_queue, &drive_state, 0) == pdTRUE) {
      if (can_publish && telemetry_enabled && (telemetry_mask & ROS2_TELEM_MASK_DRIVE_STATE) != 0)
        ros2_msgs_send_drive_state(msgs, msgs->tx_seq++, &drive_state);
    }
    // ESP_LOGI(TAG, "Sending telemetry %u", msgs->tx_seq);
  }
}

static size_t ros2_msgs_read(ros2_msgs_ctx_t *msgs, uint8_t *data, size_t len)
{
  return (msgs->read != NULL) ? msgs->read(data, len) : 0;
}

void ros2_msgs_send_frame(ros2_msgs_ctx_t *msgs, uint8_t msg_type, uint8_t seq, const uint8_t *payload,
                          size_t payload_len)
{
  if (tx_mutex != NULL)
    xSemaphoreTake(tx_mutex, portMAX_DELAY);

  const size_t written = framed_link_send_frame(&msgs->link, msg_type, seq, payload, payload_len);
  if (written == 0)
    ESP_LOGW(TAG, "Frame TX failed type=0x%02X seq=%u", msg_type, seq);

  if (tx_mutex != NULL)
    xSemaphoreGive(tx_mutex);
}

static void ros2_msgs_send_ack(ros2_msgs_ctx_t *msgs, uint8_t seq)
{
  uint8_t payload = seq;
  ros2_msgs_send_frame(msgs, FRAMED_LINK_MSG_ACK, seq, &payload, 1);
}

static void ros2_msgs_send_nack(ros2_msgs_ctx_t *msgs, uint8_t seq, uint8_t err)
{
  uint8_t payload[2] = {seq, err};
  ros2_msgs_send_frame(msgs, FRAMED_LINK_MSG_NACK, seq, payload, sizeof(payload));
}

void ros2_msgs_send_battery_state(ros2_msgs_ctx_t *msgs, uint8_t seq)
{
  uint8_t payload[256];
  size_t len = 0;
  battery_data_t battery_data = {0};
  const esp_err_t battery_ret = battery_fetch_data(&battery_data);

  payload[len++] = (battery_ret == ESP_OK && battery_data.valid) ? 1 : 0;
  payload[len++] = (uint8_t)battery_ret;

  memcpy(payload + len, &battery_data.timestamp, sizeof(battery_data.timestamp));
  len += sizeof(battery_data.timestamp);
  memcpy(payload + len, &battery_data.voltage, sizeof(battery_data.voltage));
  len += sizeof(battery_data.voltage);
  memcpy(payload + len, &battery_data.current, sizeof(battery_data.current));
  len += sizeof(battery_data.current);
  memcpy(payload + len, &battery_data.power, sizeof(battery_data.power));
  len += sizeof(battery_data.power);
  memcpy(payload + len, &battery_data.energy, sizeof(battery_data.energy));
  len += sizeof(battery_data.energy);

  ros2_msgs_send_frame(msgs, ROS2_MSG_TELEMETRY_BATTERY_STATE, seq, payload, len);
}

void ros2_msgs_send_drive_state(ros2_msgs_ctx_t *msgs, uint8_t seq, const drive_state_t *state)
{
  if (state == NULL)
    return;

  uint8_t payload[ROS2_TELEMETRY_DRIVE_STATE_PAYLOAD_SIZE];
  size_t len = 0;
  memcpy(payload + len, &state->timestamp_ms, sizeof(state->timestamp_ms));
  len += sizeof(state->timestamp_ms);
  memcpy(payload + len, &state->linear_velocity, sizeof(state->linear_velocity));
  len += sizeof(state->linear_velocity);
  memcpy(payload + len, &state->angular_velocity, sizeof(state->angular_velocity));
  len += sizeof(state->angular_velocity);
  memcpy(payload + len, &state->left_velocity, sizeof(state->left_velocity));
  len += sizeof(state->left_velocity);
  memcpy(payload + len, &state->right_velocity, sizeof(state->right_velocity));
  len += sizeof(state->right_velocity);
  ros2_msgs_send_frame(msgs, ROS2_MSG_TELEMETRY_DRIVE_STATE, seq, payload, len);
}

static void ros2_msgs_send_imu_state_sample(ros2_msgs_ctx_t *msgs, uint8_t seq, const navigation_imu_sample_t *imu)
{
  if (imu == NULL)
    return;

  uint8_t payload[ROS2_TELEMETRY_IMU_STATE_PAYLOAD_SIZE];
  size_t len = 0;

  payload[len++] = imu->data_valid ? 1 : 0;
  payload[len++] = imu->data_valid ? 0 : 1;

  memcpy(payload + len, &imu->timestamp_us, sizeof(imu->timestamp_us));
  len += sizeof(imu->timestamp_us);
  memcpy(payload + len, imu->acceleration_mps2, sizeof(imu->acceleration_mps2));
  len += sizeof(imu->acceleration_mps2);
  memcpy(payload + len, imu->angular_velocity_rad_s, sizeof(imu->angular_velocity_rad_s));
  len += sizeof(imu->angular_velocity_rad_s);
  memcpy(payload + len, imu->magnetic_field_uT, sizeof(imu->magnetic_field_uT));
  len += sizeof(imu->magnetic_field_uT);
  memcpy(payload + len, imu->quaternion, sizeof(imu->quaternion));
  len += sizeof(imu->quaternion);

  if (len != sizeof(payload)) {
    ESP_LOGE(TAG, "Unexpected IMU telemetry payload size: %u", (unsigned)len);
    return;
  }
  ros2_msgs_send_frame(msgs, ROS2_MSG_TELEMETRY_IMU_STATE, seq, payload, len);
}

void ros2_msgs_send_imu_state(ros2_msgs_ctx_t *msgs, uint8_t seq)
{
  navigation_snapshot_t snapshot = {0};
  const esp_err_t navigation_ret = navigation_get_snapshot(&snapshot);
  if (navigation_ret != ESP_OK && !snapshot.imu_valid)
    return;
  ros2_msgs_send_imu_state_sample(msgs, seq, &snapshot.imu);
}

#ifdef UNIT_TEST
void ros2_msgs_test_set_write(ros2_msgs_write_fn_t write)
{
  ros2_ctx.write = write;
  ros2_ctx.link.write = write;
}

void ros2_msgs_test_process_frame(uint8_t *data, size_t len) { framed_link_process(&ros2_ctx.link, data, len); }
#endif

static void ros2_msgs_handle_message(ros2_msgs_ctx_t *msgs, uint8_t msg_type, uint8_t seq, const uint8_t *payload,
                                     size_t len)
{
  diag_log_ros2(msg_type, seq, payload, len);
  if (ros2_monitor != NULL)
    ros2_monitor(msg_type, seq, payload, len);

  switch (msg_type) {
  case ROS2_MSG_HEARTBEAT:
    ESP_LOGD(TAG, "Received HEARTBEAT seq=%u", seq);
    ros2_msgs_send_ack(msgs, seq);
    break;

  case ROS2_MSG_CMD_MOTOR:
    if (len == 8) {
      float left_mps;
      float right_mps;
      memcpy(&left_mps, payload, sizeof(left_mps));
      memcpy(&right_mps, payload + sizeof(left_mps), sizeof(right_mps));
      ESP_LOGD(TAG, "Received CMD_MOTOR seq=%u left=%.3f m/s right=%.3f m/s", seq, left_mps, right_mps);
      if (motor_set(left_mps, right_mps))
        ros2_msgs_send_ack(msgs, seq);
      else
        ros2_msgs_send_nack(msgs, seq, ROS2_MSG_ERR_RANGE);
    } else {
      ros2_msgs_send_nack(msgs, seq, ROS2_MSG_ERR_LEN);
    }
    break;

  case ROS2_MSG_CMD_SERVO:
    if (len == 3) {
      const uint8_t channel = payload[0];
      const uint16_t pulse = payload[1] | (payload[2] << 8);
      ESP_LOGD(TAG, "Received CMD_SERVO seq=%u channel=%u pulse=%u", seq, channel, pulse);
      ros2_msgs_send_ack(msgs, seq);
    } else {
      ros2_msgs_send_nack(msgs, seq, ROS2_MSG_ERR_LEN);
    }
    break;

  case ROS2_MSG_CMD_CONFIG:
    if (len == 5) {
      const uint8_t key = payload[0];
      const uint32_t raw_value = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) | ((uint32_t)payload[3] << 16) |
                                 ((uint32_t)payload[4] << 24);
      const int32_t value = (int32_t)raw_value;

      switch (key) {
      case ROS2_CFG_TELEM_ENABLE:
        ros2_msgs_set_telemetry_enabled(value != 0);

        ESP_LOGI(TAG, "Telemetry %s", telemetry_enabled ? "enabled" : "disabled");
        break;

      case ROS2_CFG_TELEM_RATE_MS:
        if (value >= 10 && value <= 5000) {
          telemetry_period_ms = (uint32_t)value;
          if (telemetry_timer != NULL)
            xTimerChangePeriod(telemetry_timer, pdMS_TO_TICKS(telemetry_period_ms), 0);
          ESP_LOGI(TAG, "Telemetry rate set to %u ms", telemetry_period_ms);
        } else {
          ros2_msgs_send_nack(msgs, seq, ROS2_MSG_ERR_RANGE);
          return;
        }
        break;

      case ROS2_CFG_TELEM_MASK:
        telemetry_mask = (uint32_t)value;
        ESP_LOGI(TAG, "Telemetry mask set to 0x%08X", telemetry_mask);
        break;

      default:
        ros2_msgs_send_nack(msgs, seq, ROS2_MSG_ERR_CFG);
        return;
      }

      ESP_LOGI(TAG, "CONFIG seq=%u key=%u value=%ld", seq, key, (long)value);

      ros2_msgs_send_ack(msgs, seq);
    } else {
      ros2_msgs_send_nack(msgs, seq, ROS2_MSG_ERR_LEN);
    }
    break;

  case ROS2_MSG_SET_TIME:
    if (len == sizeof(uint64_t)) {
      uint64_t unix_seconds;
      memcpy(&unix_seconds, payload, sizeof(unix_seconds));
      if (ros2_msgs_set_utc(unix_seconds)) {
        ESP_LOGI(TAG, "UTC set to Unix timestamp %llu", (unsigned long long)unix_seconds);
        ros2_msgs_send_ack(msgs, seq);
      } else {
        ros2_msgs_send_nack(msgs, seq, ROS2_MSG_ERR_RANGE);
      }
    } else {
      ros2_msgs_send_nack(msgs, seq, ROS2_MSG_ERR_LEN);
    }
    break;

  default:
    led_set(16, 0, 0);
    ESP_LOGW(TAG, "Unknown message type 0x%02X seq=%u", msg_type, seq);
    ros2_msgs_send_nack(msgs, seq, ROS2_MSG_ERR_TYPE);
    break;
  }
}

static void ros2_msgs_handle_frame(void *ctx, uint8_t msg_type, uint8_t seq, const uint8_t *payload, size_t len)
{
  ros2_msgs_handle_message((ros2_msgs_ctx_t *)ctx, msg_type, seq, payload, len);
}

void ros2_msgs_init(void)
{
#ifndef UNIT_TEST
  ros2_msgs_load_settings();
  runtime_start_us = esp_timer_get_time();
#endif

#if defined(CONFIG_ROS2_TRANSPORT_UART) && CONFIG_ROS2_TRANSPORT_UART
  uart_bridge_init();
#else
  usb_bridge_init();
  ros2_ctx.write = usb_bridge_write_bytes;
  ros2_ctx.read = usb_bridge_read_bytes;
  framed_link_init(&ros2_ctx.link, usb_bridge_write_bytes, &ros2_ctx, ros2_msgs_handle_frame, &ros2_ctx);
#endif
  // memset(msgs, 0, sizeof(*msgs));
  ESP_LOGI(TAG, "ros2 msgs service init");

  tx_mutex = xSemaphoreCreateMutex();
  ESP_ERROR_CHECK(tx_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);
  imu_state_tx_queue = xQueueCreate(1, sizeof(navigation_imu_sample_t));
  ESP_ERROR_CHECK(imu_state_tx_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
  battery_state_tx_queue = xQueueCreate(1, sizeof(uint8_t));
  ESP_ERROR_CHECK(battery_state_tx_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
  drive_state_tx_queue = xQueueCreate(1, sizeof(drive_state_t));
  ESP_ERROR_CHECK(drive_state_tx_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);

  telemetry_timer = xTimerCreate("telemetry_tmr", pdMS_TO_TICKS(telemetry_period_ms), pdTRUE, /* auto reload */
                                 NULL, telemetry_timer_cb);

  xTaskCreate(ros2_command_task, "ros2_command", 4096, &ros2_ctx, 5, &command_task);
  xTaskCreate(ros2_telemetry_task, "ros2_telemetry", 4096, &ros2_ctx, 5, &telemetry_task);
  navigation_set_telemetry_callback(ros2_imu_state_callback);
#ifndef UNIT_TEST
  xTaskCreate(ros2_runtime_task, "ros2_runtime", 3072, NULL, 2, &runtime_task);
#endif
  usb_bridge_set_callback((usb_bridge_cb_t)ros2_msgs_on_rx);

  if (telemetry_enabled)
    xTimerStart(telemetry_timer, 0);
}
