#include "diag_host_stubs.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

diag_host_state_t diag_host_state;
static void (*battery_task)(void *);
static vprintf_like_t log_callback;
static jmp_buf task_exit;
static bool task_running;

static int forwarding_vprintf(const char *format, va_list args)
{
  (void)format;
  (void)args;
  diag_host_state.forwarded_log_calls++;
  return 123;
}

void diag_host_reset(void)
{
  memset(&diag_host_state, 0, sizeof(diag_host_state));
  diag_host_state.battery_result = -2;
  battery_task = NULL;
  log_callback = NULL;
  task_running = false;
}

void *heap_caps_calloc(size_t count, size_t size, unsigned int caps)
{
  (void)caps;
  diag_host_state.allocation_calls++;
  if ((int)diag_host_state.allocation_calls == diag_host_state.fail_allocation_call)
    return NULL;
  return calloc(count, size);
}

void heap_caps_free(void *ptr)
{
  if (ptr != NULL)
    diag_host_state.free_calls++;
  free(ptr);
}

vprintf_like_t esp_log_set_vprintf(vprintf_like_t func)
{
  vprintf_like_t previous = diag_host_state.return_previous_log_callback ? forwarding_vprintf : log_callback;
  log_callback = func;
  return previous;
}

void diag_host_set_return_previous_log_callback(bool enabled)
{
  diag_host_state.return_previous_log_callback = enabled;
}

int diag_host_emit_log(const char *format, ...)
{
  if (log_callback == NULL)
    return -1;

  va_list args;
  va_start(args, format);
  const int result = log_callback(format, args);
  va_end(args);
  return result;
}

int64_t esp_timer_get_time(void) { return diag_host_state.time_us; }

BaseType_t xTaskCreate(void (*task)(void *), const char *name, uint32_t stack_depth, void *arg, UBaseType_t priority,
                       TaskHandle_t *task_handle)
{
  (void)name;
  (void)stack_depth;
  (void)arg;
  (void)priority;
  battery_task = task;
  diag_host_state.battery_task_registered = true;
  if (diag_host_state.task_create_fails) {
    if (task_handle != NULL)
      *task_handle = NULL;
    return 0;
  }
  if (task_handle != NULL)
    *task_handle = (TaskHandle_t)task;
  return pdPASS;
}

void vTaskDelay(TickType_t ticks)
{
  (void)ticks;
  if (task_running)
    longjmp(task_exit, 1);
}

esp_err_t battery_fetch_data(battery_data_t *data)
{
  *data = diag_host_state.battery_data;
  return diag_host_state.battery_result;
}

void diag_host_run_battery_task_once(void)
{
  if (battery_task == NULL)
    return;
  task_running = true;
  if (setjmp(task_exit) == 0)
    battery_task(NULL);
  task_running = false;
}