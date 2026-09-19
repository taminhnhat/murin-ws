#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "battery.h"

typedef struct {
  unsigned int allocation_calls;
  unsigned int free_calls;
  int fail_allocation_call;
  bool task_create_fails;
  bool return_previous_log_callback;
  unsigned int forwarded_log_calls;
  bool battery_task_registered;
  battery_data_t battery_data;
  int battery_result;
  int64_t time_us;
} diag_host_state_t;

extern diag_host_state_t diag_host_state;

void diag_host_reset(void);
int diag_host_emit_log(const char *format, ...);
void diag_host_set_return_previous_log_callback(bool enabled);
void diag_host_run_battery_task_once(void);