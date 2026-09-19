#include "shell_command.h"
#include "shell_uart.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "monitor.h"
#include "ros2_msgs.h"

int set_command(int argc, char **argv)
{
  if (argc != 3 || strcmp(argv[1], "telemetry") != 0) {
    shell_write("Usage: set telemetry <true|false>\r\n");
    return 1;
  }

  bool enabled;
  if (strcmp(argv[2], "true") == 0)
    enabled = true;
  else if (strcmp(argv[2], "false") == 0)
    enabled = false;
  else {
    shell_write("Usage: set telemetry <true|false>\r\n");
    return 1;
  }

  ros2_msgs_set_telemetry_enabled(enabled);
  shell_printf("Telemetry %s\r\n", enabled ? "enabled" : "disabled");
  return 0;
}

int get_command(int argc, char **argv)
{
  if (argc != 2 || strcmp(argv[1], "telemetry") != 0) {
    shell_write("Usage: get telemetry\r\n");
    return 1;
  }

  shell_printf("Telemetry %s\r\n", ros2_msgs_get_telemetry_enabled() ? "enabled" : "disabled");
  return 0;
}

int monitor_command(int argc, char **argv)
{
  if (argc != 2 || (strcmp(argv[1], "ros2") != 0 && strcmp(argv[1], "rp3") != 0 && strcmp(argv[1], "diff_drive") != 0 &&
                    strcmp(argv[1], "navigation") != 0 && strcmp(argv[1], "system") != 0)) {
    shell_write("Usage: monitor <ros2|rp3|diff_drive|navigation|system>\r\n");
    return 1;
  }

  const bool rp3 = strcmp(argv[1], "rp3") == 0;
  const bool diff_drive = strcmp(argv[1], "diff_drive") == 0;
  const bool navigation = strcmp(argv[1], "navigation") == 0;
  const bool system = strcmp(argv[1], "system") == 0;
  monitor_ros2_enable(!rp3 && !diff_drive && !navigation && !system);
  monitor_rp3_enable(rp3);
  monitor_diff_drive_enable(diff_drive);
  monitor_navigation_enable(navigation);
  monitor_system_enable(system);
  shell_write("\033[?25l");
  shell_printf("%s monitor enabled (press q or Ctrl-C to stop)\r\n",
               rp3 ? "RP3" : (diff_drive ? "DIFF_DRIVE" : (navigation ? "NAVIGATION" : (system ? "SYSTEM" : "ROS2"))));
  return 0;
}

static const char *chip_model_name(esp_chip_model_t model)
{
  switch (model) {
  case CHIP_ESP32:
    return "ESP32";
  case CHIP_ESP32S2:
    return "ESP32-S2";
  case CHIP_ESP32S3:
    return "ESP32-S3";
  case CHIP_ESP32C3:
    return "ESP32-C3";
  case CHIP_ESP32C2:
    return "ESP32-C2";
  case CHIP_ESP32C6:
    return "ESP32-C6";
  case CHIP_ESP32H2:
    return "ESP32-H2";
  default:
    return "unknown";
  }
}

int stats_command(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  shell_write("=== System status ===\r\n");
  shell_printf("Chip: %s revision %d, %d core(s)\r\n", chip_model_name(chip_info.model), chip_info.revision,
               chip_info.cores);
  shell_printf("Uptime: %" PRIu64 " ms\r\n", esp_timer_get_time() / 1000ULL);
  shell_printf("Total runtime: %" PRIu64 " ms\r\n", ros2_msgs_get_total_runtime_ms());
  shell_printf("Heap: free=%" PRIu32 " minimum=%" PRIu32 " largest=%" PRIu32 " bytes\r\n", esp_get_free_heap_size(),
               esp_get_minimum_free_heap_size(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  shell_printf("Internal heap: free=%" PRIu32 " bytes\r\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  shell_printf("PSRAM: free=%" PRIu32 " total=%" PRIu32 " bytes\r\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
               heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
  shell_printf("Tasks: %" PRIu32 "\r\n", (uint32_t)uxTaskGetNumberOfTasks());

  return 0;
}

int diag_command(int argc, char **argv)
{
  if (argc != 3 || (strcmp(argv[1], "rp3") != 0 && strcmp(argv[1], "battery") != 0 && strcmp(argv[1], "ros2") != 0 &&
                    strcmp(argv[1], "system") != 0)) {
    shell_write("Usage: diag <rp3|battery|ros2|system> <1-20>\r\n");
    return 1;
  }

  char *end = NULL;
  unsigned long requested = strtoul(argv[2], &end, 10);
  if (argv[2][0] == '\0' || *end != '\0' || requested == 0 || requested > 20) {
    shell_write("Usage: diag <rp3|battery|ros2|system> <1-20>\r\n");
    return 1;
  }

  if (strcmp(argv[1], "rp3") == 0) {
    static rp3_signal_sample_t records[20];
    size_t record_count = diag_get_rp3_logs(records, requested);
    shell_printf("RP3 logs: %u record(s)\r\n", (unsigned)record_count);
    for (size_t i = 0; i < record_count; i++) {
      const rp3_signal_sample_t *sample = &records[i];
      shell_printf("%" PRIi64 " us: UL RSSI=%d SNR=%d LQ=%u ANT=%u "
                   "DL RSSI=%d SNR=%d LQ=%u RF=%u TX=%u\r\n",
                   sample->timestamp_us, sample->uplink_rssi_dbm, sample->uplink_snr_db,
                   (unsigned)sample->uplink_link_quality, (unsigned)sample->active_antenna, sample->downlink_rssi_dbm,
                   sample->downlink_snr_db, (unsigned)sample->downlink_link_quality, (unsigned)sample->rf_mode,
                   (unsigned)sample->tx_power);
      if (sample->rc_channels_valid) {
        shell_write("  RC:");
        for (size_t channel = 0; channel < 16; channel++)
          shell_printf(" %u", (unsigned)sample->rc_channels[channel]);
        shell_write("\r\n");
      }
    }
    return 0;
  }

  if (strcmp(argv[1], "battery") == 0) {
    static battery_diag_record_t records[20];
    size_t record_count = diag_get_battery_logs(records, requested);
    shell_printf("Battery logs: %u record(s)\r\n", (unsigned)record_count);
    for (size_t i = 0; i < record_count; i++) {
      const battery_diag_record_t *record = &records[i];
      shell_printf("%" PRIi64 " us: status=%d valid=%s V=%.3f I=%.3f P=%.3f\r\n", record->timestamp_us,
                   (int)record->status, record->data.valid ? "YES" : "NO", record->data.voltage, record->data.current,
                   record->data.power);
    }
    return 0;
  }

  if (strcmp(argv[1], "system") == 0) {
    static system_log_record_t records[20];
    size_t record_count = diag_get_system_logs(records, requested);
    shell_printf("System logs: %u record(s)\r\n", (unsigned)record_count);
    for (size_t i = 0; i < record_count; i++)
      shell_printf("%" PRIi64 " us: %s", records[i].timestamp_us, records[i].message);
    return 0;
  }

  static ros2_diag_message_t records[20];
  size_t record_count = diag_get_ros2_logs(records, requested);
  shell_printf("ROS2 logs: %u message(s)\r\n", (unsigned)record_count);
  for (size_t i = 0; i < record_count; i++) {
    const ros2_diag_message_t *message = &records[i];
    shell_printf("%" PRIi64 " us: type=0x%02X seq=%u len=%u payload=", message->timestamp_us,
                 (unsigned)message->msg_type, (unsigned)message->seq, (unsigned)message->payload_len);
    for (size_t byte = 0; byte < message->payload_len; byte++)
      shell_printf("%02X", (unsigned)message->payload[byte]);
    shell_write("\r\n");
  }
  return 0;
}

int help_command(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  shell_write("Available commands:\r\n");
  shell_write("  stats  Print system status\r\n");
  shell_write("  diag rp3 <1-20>  Print latest RP3 telemetry logs\r\n");
  shell_write("  diag battery <1-20>  Print latest battery diagnostics\r\n");
  shell_write("  diag ros2 <1-20>  Print latest ROS2 messages\r\n");
  shell_write("  diag system <1-20>  Print latest system logs\r\n");
  shell_write("  set telemetry <true|false>  Enable or disable auto telemetry\r\n");
  shell_write("  get telemetry  Print auto telemetry status\r\n");
  shell_write("  monitor ros2  Monitor incoming ROS2 commands\r\n");
  shell_write("  monitor rp3  Monitor RP3 channels\r\n");
  shell_write("  monitor diff_drive  Monitor motor PWM, brakes, and directions\r\n");
  shell_write("  monitor navigation  Monitor BNO085 navigation data\r\n");
  shell_write("  monitor system  Monitor system logs\r\n");
  shell_write("  q/Ctrl-C  Stop the active monitor\r\n");
  shell_write("  clear  Clear the terminal screen\r\n");
  return 0;
}

int clear_command(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  shell_write("\x1b[2J\x1b[H");
  return 0;
}
