#include "system_log.h"

#include <stdarg.h>
#include <stdio.h>

#include "diag.h"
#include "esp_log.h"
#include "esp_timer.h"

static vprintf_like_t s_default_vprintf;
static system_log_monitor_fn_t s_monitor;

static int system_log_vprintf(const char *format, va_list args)
{
  system_log_record_t record = {
      .timestamp_us = esp_timer_get_time(),
  };
  va_list copy;

  va_copy(copy, args);
  vsnprintf(record.message, sizeof(record.message), format, copy);
  va_end(copy);

  diag_log_system_record(&record);
  if (s_monitor != NULL)
    s_monitor(&record);

  if (s_default_vprintf != NULL)
    return s_default_vprintf(format, args);
  return vprintf(format, args);
}

void system_log_init(void) { s_default_vprintf = esp_log_set_vprintf(system_log_vprintf); }

void system_log_set_monitor(system_log_monitor_fn_t monitor) { s_monitor = monitor; }
