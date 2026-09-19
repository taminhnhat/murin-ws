#include "shell_uart.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_console.h"
#include "esp_log.h"
#include "flash_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "monitor.h"
#include "sdkconfig.h"
#include "shell_command.h"

#ifndef CONFIG_ROS2_TRANSPORT_UART_BAUD_RATE
#define CONFIG_ROS2_TRANSPORT_UART_BAUD_RATE 115200
#endif

#ifndef CONFIG_ROS2_TRANSPORT_UART_TX_GPIO
#define CONFIG_ROS2_TRANSPORT_UART_TX_GPIO 43
#endif

#ifndef CONFIG_ROS2_TRANSPORT_UART_RX_GPIO
#define CONFIG_ROS2_TRANSPORT_UART_RX_GPIO 44
#endif

#define SHELL_UART_PORT UART_NUM_1
#define SHELL_UART_BAUD_RATE CONFIG_ROS2_TRANSPORT_UART_BAUD_RATE
#define SHELL_UART_TX_GPIO CONFIG_ROS2_TRANSPORT_UART_TX_GPIO
#define SHELL_UART_RX_GPIO CONFIG_ROS2_TRANSPORT_UART_RX_GPIO
#define SHELL_UART_RX_BUFFER_SIZE 512
#define SHELL_LINE_MAX_LENGTH 128
#define SHELL_HISTORY_SIZE 20
#define SHELL_TASK_STACK_SIZE 8192
#define SHELL_HISTORY_MAGIC 0x53484953U

typedef struct {
  uint32_t magic;
  uint32_t count;
  char entries[SHELL_HISTORY_SIZE][SHELL_LINE_MAX_LENGTH];
} shell_history_storage_t;

static const char *TAG = "shell";

void shell_write(const char *text) { uart_write_bytes(SHELL_UART_PORT, text, strlen(text)); }

void shell_printf(const char *format, ...)
{
  char buffer[192];
  va_list args;

  va_start(args, format);
  int length = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  if (length > 0)
    uart_write_bytes(SHELL_UART_PORT, buffer, (size_t)length < sizeof(buffer) ? (size_t)length : sizeof(buffer) - 1);
}

static void shell_replace_line(char *line, size_t *line_length, const char *replacement, size_t replacement_length)
{
  while (*line_length > 0) {
    shell_write("\b \b");
    (*line_length)--;
  }

  memcpy(line, replacement, replacement_length);
  *line_length = replacement_length;
  uart_write_bytes(SHELL_UART_PORT, line, *line_length);
}

static size_t shell_history_load(char history[SHELL_HISTORY_SIZE][SHELL_LINE_MAX_LENGTH])
{
  shell_history_storage_t stored = {0};
  size_t stored_size = sizeof(stored);
  if (flash_storage_get(FLASH_STORAGE_ITEM_SHELL_HISTORY, &stored, &stored_size) != ESP_OK ||
      stored_size != sizeof(stored) || stored.magic != SHELL_HISTORY_MAGIC || stored.count > SHELL_HISTORY_SIZE)
    return 0;

  for (size_t i = 0; i < stored.count; i++) {
    if (memchr(stored.entries[i], '\0', SHELL_LINE_MAX_LENGTH) == NULL)
      return 0;
    memcpy(history[i], stored.entries[i], SHELL_LINE_MAX_LENGTH);
  }
  return stored.count;
}

static void shell_history_save(char history[SHELL_HISTORY_SIZE][SHELL_LINE_MAX_LENGTH], size_t history_count)
{
  shell_history_storage_t stored = {
      .magic = SHELL_HISTORY_MAGIC,
      .count = history_count,
  };
  memcpy(stored.entries, history, sizeof(stored.entries));
  flash_storage_set(FLASH_STORAGE_ITEM_SHELL_HISTORY, &stored, sizeof(stored));
}

static void shell_task(void *arg)
{
  (void)arg;
  char line[SHELL_LINE_MAX_LENGTH];
  char history[SHELL_HISTORY_SIZE][SHELL_LINE_MAX_LENGTH];
  char history_draft[SHELL_LINE_MAX_LENGTH];
  size_t line_length = 0;
  size_t cursor_pos = 0;
  size_t history_count = shell_history_load(history);
  int history_index = -1;
  size_t draft_length = 0;
  uint8_t escape_state = 0;
  uint8_t ch;

  shell_write("\033[?25h\r\nshell> ");

  while (true) {
    if (uart_read_bytes(SHELL_UART_PORT, &ch, 1, portMAX_DELAY) != 1)
      continue;

    // Handle the ANSI sequence emitted by arrow keys (for example, Up is ESC [
    // A).
    if (escape_state == 1) {
      escape_state = ch == '[' ? 2 : 0;
      continue;
    }
    if (escape_state == 2) {
      if (ch == '3') {
        escape_state = 3;
        continue;
      }

      escape_state = 0;
      if (ch == 'A' && history_count > 0) {
        if (history_index < 0) {
          memcpy(history_draft, line, line_length);
          draft_length = line_length;
          history_index = 0;
        } else if (history_index + 1 < (int)history_count)
          history_index++;

        shell_replace_line(line, &line_length, history[history_index], strlen(history[history_index]));
        cursor_pos = line_length;
      } else if (ch == 'B' && history_index >= 0) {
        if (history_index == 0) {
          history_index = -1;
          shell_replace_line(line, &line_length, history_draft, draft_length);
        } else {
          history_index--;
          shell_replace_line(line, &line_length, history[history_index], strlen(history[history_index]));
        }
        cursor_pos = line_length;
      } else if (ch == 'D' && cursor_pos > 0) {
        shell_write("\b");
        cursor_pos--;
      } else if (ch == 'C' && cursor_pos < line_length) {
        uart_write_bytes(SHELL_UART_PORT, &line[cursor_pos], 1);
        cursor_pos++;
      }
      continue;
    }
    if (escape_state == 3) {
      escape_state = 0;
      if (ch == '~' && cursor_pos < line_length) {
        history_index = -1;
        memmove(&line[cursor_pos], &line[cursor_pos + 1], line_length - cursor_pos - 1);
        line_length--;

        uart_write_bytes(SHELL_UART_PORT, &line[cursor_pos], line_length - cursor_pos);
        shell_write(" ");
        for (size_t i = cursor_pos; i <= line_length; i++)
          shell_write("\b");
      }
      continue;
    }
    if (ch == 0x1b) {
      escape_state = 1;
      continue;
    }

    /* Allow the active monitor to be stopped without a command line. */
    if ((monitor_ros2_is_enable() || monitor_rp3_is_enable() || monitor_diff_drive_is_enable() ||
         monitor_navigation_is_enable() || monitor_system_is_enable()) &&
        (ch == 0x03 || (ch == 'q' && line_length == 0))) {
      monitor_ros2_enable(false);
      monitor_rp3_enable(false);
      monitor_diff_drive_enable(false);
      monitor_navigation_enable(false);
      monitor_system_enable(false);
      shell_write("\r\n\033[2K\033[?25hMonitor disabled\r\n");
      line_length = 0;
      cursor_pos = 0;
      history_index = -1;
      if (!monitor_ros2_is_enable() && !monitor_rp3_is_enable() && !monitor_diff_drive_is_enable() &&
          !monitor_navigation_is_enable() && !monitor_system_is_enable())
        shell_write("shell> ");
      continue;
    }

    if (ch == '\r' || ch == '\n') {
      int command_ret = 0;
      esp_err_t parse_ret = ESP_OK;
      line[line_length] = '\0';
      shell_write("\r\n");

      if (line_length > 0) {
        const bool command_differs_from_last = history_count == 0 || strcmp(history[0], line) != 0;
        if (command_differs_from_last) {
          size_t entries_to_move = history_count < SHELL_HISTORY_SIZE ? history_count : SHELL_HISTORY_SIZE - 1;
          if (entries_to_move > 0)
            memmove(history[1], history[0], entries_to_move * sizeof(history[0]));
          memcpy(history[0], line, line_length + 1);
          if (history_count < SHELL_HISTORY_SIZE)
            history_count++;
          shell_history_save(history, history_count);
        }
        parse_ret = esp_console_run(line, &command_ret);
      }

      if (parse_ret != ESP_OK)
        shell_printf("Error: could not parse command: %s\r\n", esp_err_to_name(parse_ret));
      else if (command_ret != 0)
        shell_printf("Error: command failed (%d)\r\n", command_ret);

      line_length = 0;
      cursor_pos = 0;
      history_index = -1;
      shell_write("shell> ");
    } else if (ch == '\b' || ch == 0x7f) {
      if (cursor_pos > 0) {
        history_index = -1;
        cursor_pos--;
        shell_write("\b");
        memmove(&line[cursor_pos], &line[cursor_pos + 1], line_length - cursor_pos - 1);
        line_length--;

        uart_write_bytes(SHELL_UART_PORT, &line[cursor_pos], line_length - cursor_pos);
        shell_write(" ");
        for (size_t i = cursor_pos; i <= line_length; i++)
          shell_write("\b");
      }
    } else if (isprint((int)ch)) {
      if (line_length < sizeof(line) - 1) {
        history_index = -1;
        memmove(&line[cursor_pos + 1], &line[cursor_pos], line_length - cursor_pos);
        line[cursor_pos++] = (char)ch;
        line_length++;

        uart_write_bytes(SHELL_UART_PORT, &line[cursor_pos - 1], line_length - cursor_pos + 1);
        for (size_t i = cursor_pos; i < line_length; i++)
          shell_write("\b");
      }
    }
  }
}

void shell_uart_init(void)
{
  const uart_config_t uart_config = {
      .baud_rate = SHELL_UART_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  const esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
  const esp_console_cmd_t stats_cmd = {
      .command = "stats",
      .help = "System statistics commands",
      .func = &stats_command,
  };
  const esp_console_cmd_t diag_cmd = {
      .command = "diag",
      .help = "Diagnostic commands",
      .hint = "rp3",
      .func = &diag_command,
  };
  const esp_console_cmd_t set_cmd = {
      .command = "set",
      .help = "Set runtime configuration",
      .hint = "telemetry <true|false>",
      .func = &set_command,
  };
  const esp_console_cmd_t get_cmd = {
      .command = "get",
      .help = "Get runtime configuration",
      .hint = "telemetry",
      .func = &get_command,
  };
  const esp_console_cmd_t monitor_cmd = {
      .command = "monitor",
      .help = "Live monitoring commands",
      .hint = "<ros2|rp3>",
      .func = &monitor_command,
  };
  const esp_console_cmd_t help_cmd = {
      .command = "help",
      .help = "List available commands",
      .func = &help_command,
  };
  const esp_console_cmd_t clear_cmd = {
      .command = "clear",
      .help = "Clear the terminal screen",
      .func = &clear_command,
  };

  ESP_LOGI(TAG, "Initializing UART shell port=%d tx=%d rx=%d baud=%d", SHELL_UART_PORT, SHELL_UART_TX_GPIO,
           SHELL_UART_RX_GPIO, SHELL_UART_BAUD_RATE);
  ESP_ERROR_CHECK(uart_driver_install(SHELL_UART_PORT, SHELL_UART_RX_BUFFER_SIZE, 0, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(SHELL_UART_PORT, &uart_config));
  ESP_ERROR_CHECK(
      uart_set_pin(SHELL_UART_PORT, SHELL_UART_TX_GPIO, SHELL_UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(esp_console_init(&console_config));

  ESP_ERROR_CHECK(esp_console_cmd_register(&stats_cmd));
  ESP_ERROR_CHECK(esp_console_cmd_register(&diag_cmd));
  ESP_ERROR_CHECK(esp_console_cmd_register(&set_cmd));
  ESP_ERROR_CHECK(esp_console_cmd_register(&get_cmd));
  ESP_ERROR_CHECK(esp_console_cmd_register(&monitor_cmd));
  ESP_ERROR_CHECK(esp_console_cmd_register(&help_cmd));
  ESP_ERROR_CHECK(esp_console_cmd_register(&clear_cmd));

  monitor_init();
  xTaskCreate(shell_task, "shell_uart", SHELL_TASK_STACK_SIZE, NULL, 5, NULL);
}
