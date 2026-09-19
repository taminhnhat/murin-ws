#include "monitor.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "diag.h"
#include "diff_drive.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "navigation.h"
#include "ros2_msgs.h"
#include "shell_uart.h"
#include "system_log.h"

#define ROS2_MONITOR_QUEUE_LENGTH 16
#define ROS2_MONITOR_TASK_STACK_SIZE 4096
#define ROS2_MONITOR_TASK_PRIORITY 4
#define RP3_MONITOR_UPDATE_PERIOD_US 100000
#define RP3_CHANNEL_VALUE_MIN 172
#define RP3_CHANNEL_VALUE_MAX 1811
#define RP3_CHANNEL_BAR_WIDTH 4
#define RP3_MONITOR_CHANNEL_COUNT 8
#define NAVIGATION_MONITOR_UPDATE_PERIOD_US 100000

typedef enum {
  MONITOR_EVENT_ROS2,
  MONITOR_EVENT_RP3,
  MONITOR_EVENT_DIFF_DRIVE,
  MONITOR_EVENT_NAVIGATION,
  MONITOR_EVENT_SYSTEM,
  MONITOR_EVENT_SYSTEM_HISTORY,
} monitor_event_type_t;

typedef struct {
  monitor_event_type_t type;

  union {
    ros2_diag_message_t ros2;
    rp3_signal_sample_t rp3;
    diff_drive_state_t diff_drive;
    navigation_imu_sample_t navigation;
    system_log_record_t system;
  } data;
} monitor_event_t;

static const char *TAG = "monitor";
static QueueHandle_t s_monitor_queue;
static volatile bool s_monitor_enabled;
static volatile bool s_rp3_monitor_enabled;
static volatile bool s_diff_drive_monitor_enabled;
static volatile bool s_navigation_monitor_enabled;
static volatile bool s_system_monitor_enabled;
static volatile bool s_system_history_replaying;
static volatile bool s_rp3_display_started;
static bool s_navigation_display_started;
static rp3_signal_sample_t s_rp3_displayed_sample;
static bool s_rp3_display_valid;

static bool rp3_display_sample_changed(const rp3_signal_sample_t *sample)
{
  if (!s_rp3_display_valid || sample->uplink_link_quality != s_rp3_displayed_sample.uplink_link_quality ||
      sample->uplink_rssi_dbm != s_rp3_displayed_sample.uplink_rssi_dbm ||
      sample->uplink_snr_db != s_rp3_displayed_sample.uplink_snr_db ||
      sample->downlink_link_quality != s_rp3_displayed_sample.downlink_link_quality ||
      sample->downlink_rssi_dbm != s_rp3_displayed_sample.downlink_rssi_dbm ||
      sample->downlink_snr_db != s_rp3_displayed_sample.downlink_snr_db ||
      sample->rc_channels_valid != s_rp3_displayed_sample.rc_channels_valid)
    return true;

  return sample->rc_channels_valid && memcmp(sample->rc_channels, s_rp3_displayed_sample.rc_channels,
                                             RP3_MONITOR_CHANNEL_COUNT * sizeof(sample->rc_channels[0])) != 0;
}

static void ros2_monitor_callback(uint8_t msg_type, uint8_t seq, const uint8_t *payload, size_t payload_len)
{
  if (!s_monitor_enabled || s_monitor_queue == NULL)
    return;

  monitor_event_t event = {
      .type = MONITOR_EVENT_ROS2,
      .data.ros2 =
          {
              .timestamp_us = esp_timer_get_time(),
              .msg_type = msg_type,
              .seq = seq,
              .payload_len =
                  (uint16_t)(payload_len > FRAMED_LINK_MAX_PAYLOAD_LEN ? FRAMED_LINK_MAX_PAYLOAD_LEN : payload_len),
          },
  };
  if (event.data.ros2.payload_len > 0)
    memcpy(event.data.ros2.payload, payload, event.data.ros2.payload_len);

  /* Never block the ROS2 command task on shell output or a full queue. */
  (void)xQueueSend(s_monitor_queue, &event, 0);
}

static void rp3_monitor_callback(const rp3_signal_sample_t *sample)
{
  if (!s_rp3_monitor_enabled || s_monitor_queue == NULL || sample == NULL)
    return;

  monitor_event_t event = {
      .type = MONITOR_EVENT_RP3,
      .data.rp3 = *sample,
  };
  (void)xQueueSend(s_monitor_queue, &event, 0);
}

static void diff_drive_monitor_callback(const diff_drive_state_t *state)
{
  if (!s_diff_drive_monitor_enabled || s_monitor_queue == NULL || state == NULL)
    return;

  monitor_event_t event = {
      .type = MONITOR_EVENT_DIFF_DRIVE,
      .data.diff_drive = *state,
  };
  (void)xQueueSend(s_monitor_queue, &event, 0);
}

static void navigation_monitor_callback(const navigation_imu_sample_t *sample)
{
  if (!s_navigation_monitor_enabled || s_monitor_queue == NULL || sample == NULL)
    return;

  monitor_event_t event = {
      .type = MONITOR_EVENT_NAVIGATION,
      .data.navigation = *sample,
  };
  (void)xQueueSend(s_monitor_queue, &event, 0);
}

static void system_monitor_callback(const system_log_record_t *record)
{
  if (!s_system_monitor_enabled || s_system_history_replaying || s_monitor_queue == NULL || record == NULL)
    return;

  monitor_event_t event = {
      .type = MONITOR_EVENT_SYSTEM,
      .data.system = *record,
  };
  (void)xQueueSend(s_monitor_queue, &event, 0);
}

static void append_bar(char *line, size_t line_size, size_t *length, uint16_t value)
{
  const uint16_t clamped = value < RP3_CHANNEL_VALUE_MIN
                               ? RP3_CHANNEL_VALUE_MIN
                               : (value > RP3_CHANNEL_VALUE_MAX ? RP3_CHANNEL_VALUE_MAX : value);
  const unsigned range = RP3_CHANNEL_VALUE_MAX - RP3_CHANNEL_VALUE_MIN;
  const unsigned filled = ((unsigned)(clamped - RP3_CHANNEL_VALUE_MIN) * RP3_CHANNEL_BAR_WIDTH + range / 2) / range;
  if (*length >= line_size)
    return;
  *length += (size_t)snprintf(line + *length, line_size - *length, "|");
  for (unsigned i = 0; i < RP3_CHANNEL_BAR_WIDTH; i++) {
    if (*length >= line_size)
      return;
    *length += (size_t)snprintf(line + *length, line_size - *length, "%c", i < filled ? '#' : ' ');
  }
}

static void ros2_monitor_task(void *arg)
{
  (void)arg;
  monitor_event_t event;
  char line[512];
  int64_t last_rp3_update_us = 0;
  int64_t last_navigation_update_us = 0;

  while (1) {
    if (xQueueReceive(s_monitor_queue, &event, portMAX_DELAY) != pdTRUE)
      continue;

    if (event.type == MONITOR_EVENT_SYSTEM_HISTORY) {
      diag_system_log_t *records =
          heap_caps_malloc(DIAG_SYSTEM_LOG_CAPACITY * sizeof(*records), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (records != NULL) {
        const size_t record_count = diag_get_system_logs(records, DIAG_SYSTEM_LOG_CAPACITY);
        shell_printf("System log history: %u record(s)\r\n", (unsigned)record_count);
        for (size_t i = 0; i < record_count; i++)
          shell_printf("%" PRIi64 " us: %s", records[i].timestamp_us, records[i].message);
        heap_caps_free(records);
      } else {
        shell_write("Unable to allocate system log history buffer\r\n");
      }
      s_system_history_replaying = false;
      continue;
    }

    if (event.type == MONITOR_EVENT_SYSTEM) {
      if (s_system_monitor_enabled)
        shell_printf("%" PRIi64 " us: %s", event.data.system.timestamp_us, event.data.system.message);
      continue;
    }

    if (event.type == MONITOR_EVENT_RP3) {
      if (!s_rp3_monitor_enabled)
        continue;

      /* Keep only the newest pending RP3 sample.  Link statistics and
       * RC frames can arrive back-to-back for the same radio update. */
      monitor_event_t pending;
      while (xQueueReceive(s_monitor_queue, &pending, 0) == pdTRUE) {
        if (pending.type == MONITOR_EVENT_RP3)
          event = pending;
      }

      const int64_t now_us = esp_timer_get_time();
      if (now_us - last_rp3_update_us < RP3_MONITOR_UPDATE_PERIOD_US)
        continue;
      last_rp3_update_us = now_us;

      if (!rp3_display_sample_changed(&event.data.rp3))
        continue;

      const bool first_display = !s_rp3_display_started;
      size_t length = 0;
      if (first_display) {
        length = (size_t)snprintf(line, sizeof(line),
                                  "\r\033[2KLINK | UL LQ | UL RSSI | UL SNR | DL LQ | DL RSSI | DL SNR |\r\n"
                                  "\033[2K     | %3u   | %4d    | %3d    | %3u   | %4d    | %3d    |\r\n\033[2KCH   ",
                                  (unsigned)event.data.rp3.uplink_link_quality, event.data.rp3.uplink_rssi_dbm,
                                  event.data.rp3.uplink_snr_db, (unsigned)event.data.rp3.downlink_link_quality,
                                  event.data.rp3.downlink_rssi_dbm, event.data.rp3.downlink_snr_db);
      } else {
        length = (size_t)snprintf(line, sizeof(line),
                                  "\033[2F     | %3u   | %4d    | %3d    | %3u   | %4d    | %3d    |\r\n"
                                  "\033[1B\r     ",
                                  (unsigned)event.data.rp3.uplink_link_quality, event.data.rp3.uplink_rssi_dbm,
                                  event.data.rp3.uplink_snr_db, (unsigned)event.data.rp3.downlink_link_quality,
                                  event.data.rp3.downlink_rssi_dbm, event.data.rp3.downlink_snr_db);
      }
      for (size_t i = 0; first_display && i < RP3_MONITOR_CHANNEL_COUNT; i++) {
        if (length < sizeof(line))
          length += (size_t)snprintf(line + length, sizeof(line) - length, "|CH%-2u", (unsigned)(i + 1));
      }
      if (first_display && length < sizeof(line))
        length += (size_t)snprintf(line + length, sizeof(line) - length, "|");
      if (first_display && length < sizeof(line))
        length += (size_t)snprintf(line + length, sizeof(line) - length, "\r\n\033[2K     ");
      for (size_t i = 0; i < RP3_MONITOR_CHANNEL_COUNT; i++) {
        append_bar(line, sizeof(line), &length, event.data.rp3.rc_channels_valid ? event.data.rp3.rc_channels[i] : 0);
      }
      if (length < sizeof(line))
        length += (size_t)snprintf(line + length, sizeof(line) - length, "|\r");
      s_rp3_displayed_sample = event.data.rp3;
      s_rp3_display_valid = true;
      s_rp3_display_started = true;
      shell_write(line);
      continue;
    }

    if (event.type == MONITOR_EVENT_DIFF_DRIVE) {
      const diff_drive_state_t *state = &event.data.diff_drive;
      shell_printf("[DIFF] PWM=%3u %3u %3u %3u  BRAKE=%1u %1u  DIR=%1u %1u\r\n", (unsigned)state->pwm_percent[0],
                   (unsigned)state->pwm_percent[1], (unsigned)state->pwm_percent[2], (unsigned)state->pwm_percent[3],
                   state->brake[0] ? 1U : 0U, state->brake[1] ? 1U : 0U, state->direction[0] ? 1U : 0U,
                   state->direction[1] ? 1U : 0U);
      continue;
    }

    if (event.type == MONITOR_EVENT_NAVIGATION) {
      if (!s_navigation_monitor_enabled)
        continue;

      monitor_event_t pending;
      while (xQueueReceive(s_monitor_queue, &pending, 0) == pdTRUE) {
        if (pending.type == MONITOR_EVENT_NAVIGATION)
          event = pending;
      }

      const int64_t now_us = esp_timer_get_time();
      if (now_us - last_navigation_update_us < NAVIGATION_MONITOR_UPDATE_PERIOD_US)
        continue;
      last_navigation_update_us = now_us;

      const navigation_imu_sample_t *sample = &event.data.navigation;
      const bool first_display = !s_navigation_display_started;
      const char *cursor_up = first_display ? "" : "\033[2A";
      snprintf(
          line, sizeof(line),
          "%s\r\033[2K[NAV] V | ACC xyz              | GYRO xyz             | MAG xyz              | QUAT wxyz\r\n"
          "\033[2K    %u | %+.3f %+.3f %+.3f | %+.3f %+.3f %+.3f | %+.3f %+.3f %+.3f | %+.3f %+.3f %+.3f %+.3f\r\n",
          cursor_up, sample->data_valid ? 1U : 0U, sample->acceleration_mps2[0], sample->acceleration_mps2[1],
          sample->acceleration_mps2[2], sample->angular_velocity_rad_s[0], sample->angular_velocity_rad_s[1],
          sample->angular_velocity_rad_s[2], sample->magnetic_field_uT[0], sample->magnetic_field_uT[1],
          sample->magnetic_field_uT[2], sample->quaternion[0], sample->quaternion[1], sample->quaternion[2],
          sample->quaternion[3]);
      s_navigation_display_started = true;
      shell_write(line);
      continue;
    }

    if (!s_monitor_enabled)
      continue;

    const ros2_diag_message_t *message = &event.data.ros2;

    int length =
        snprintf(line, sizeof(line), "[ROS2] %" PRId64 " us type=0x%02X seq=%u len=%u payload=", message->timestamp_us,
                 (unsigned)message->msg_type, (unsigned)message->seq, (unsigned)message->payload_len);
    if (length > 0)
      shell_write(line);

    for (size_t i = 0; i < message->payload_len; i++) {
      char byte[3];
      snprintf(byte, sizeof(byte), "%02X", (unsigned)message->payload[i]);
      shell_write(byte);
    }
    shell_write("\r\n");
  }
}

void monitor_init(void)
{
  if (s_monitor_queue != NULL)
    return;

  s_monitor_queue = xQueueCreate(ROS2_MONITOR_QUEUE_LENGTH, sizeof(monitor_event_t));
  if (s_monitor_queue == NULL) {
    ESP_LOGE(TAG, "Unable to create ROS2 monitor queue");
    return;
  }

  ros2_msgs_set_monitor(ros2_monitor_callback);
  rp3_receiver_set_monitor(rp3_monitor_callback);
  motor_set_monitor_callback(diff_drive_monitor_callback);
  navigation_set_monitor(navigation_monitor_callback);
  system_log_set_monitor(system_monitor_callback);
  if (xTaskCreate(ros2_monitor_task, "monitor", ROS2_MONITOR_TASK_STACK_SIZE, NULL, ROS2_MONITOR_TASK_PRIORITY, NULL) !=
      pdPASS) {
    ESP_LOGE(TAG, "Unable to create ROS2 monitor task");
    vQueueDelete(s_monitor_queue);
    s_monitor_queue = NULL;
    ros2_msgs_set_monitor(NULL);
    rp3_receiver_set_monitor(NULL);
    motor_set_monitor_callback(NULL);
    navigation_set_monitor(NULL);
    system_log_set_monitor(NULL);
  }
}

void monitor_ros2_enable(bool enabled)
{
  s_monitor_enabled = enabled;
  if (!enabled && s_monitor_queue != NULL)
    xQueueReset(s_monitor_queue);
}

void monitor_rp3_enable(bool enabled)
{
  s_rp3_monitor_enabled = enabled;
  if (!enabled)
    s_rp3_display_started = false;
  if (!enabled)
    s_rp3_display_valid = false;
  if (!enabled && s_monitor_queue != NULL)
    xQueueReset(s_monitor_queue);
}

bool monitor_rp3_is_enable(void) { return s_rp3_monitor_enabled; }

bool monitor_ros2_is_enable(void) { return s_monitor_enabled; }

void monitor_diff_drive_enable(bool enabled)
{
  s_diff_drive_monitor_enabled = enabled;
  if (!enabled && s_monitor_queue != NULL)
    xQueueReset(s_monitor_queue);
}

bool monitor_diff_drive_is_enable(void) { return s_diff_drive_monitor_enabled; }

void monitor_navigation_enable(bool enabled)
{
  s_navigation_monitor_enabled = enabled;
  if (!enabled)
    s_navigation_display_started = false;
  if (!enabled && s_monitor_queue != NULL)
    xQueueReset(s_monitor_queue);
}

bool monitor_navigation_is_enable(void) { return s_navigation_monitor_enabled; }

void monitor_system_enable(bool enabled)
{
  s_system_monitor_enabled = enabled;
  if (!enabled && s_monitor_queue != NULL) {
    s_system_history_replaying = false;
    xQueueReset(s_monitor_queue);
  } else if (enabled && s_monitor_queue != NULL) {
    s_system_history_replaying = true;
    const monitor_event_t event = {.type = MONITOR_EVENT_SYSTEM_HISTORY};
    if (xQueueSend(s_monitor_queue, &event, 0) != pdTRUE)
      s_system_history_replaying = false;
  }
}

bool monitor_system_is_enable(void) { return s_system_monitor_enabled; }
