#ifndef DIFF_DRIVE_H
#define DIFF_DRIVE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { DRIVE_IDLE = 0, DRIVE_ACTIVE, DRIVE_FAULT } drive_safety_state_t;

typedef enum {
  DRIVE_STOP_NONE = 0,
  DRIVE_STOP_COMMAND,
  DRIVE_STOP_TIMEOUT,
  DRIVE_STOP_DISARMED,
  DRIVE_STOP_ESTOP,
  DRIVE_STOP_CRITICAL_FAULT,
  DRIVE_STOP_INVALID_COMMAND
} drive_stop_reason_t;

typedef struct {
  uint8_t pwm_percent[4];
  bool brake[2];
  bool direction[2];
  drive_safety_state_t safety_state;
  drive_stop_reason_t stop_reason;
  float target_left_mps, target_right_mps, measured_left_mps, measured_right_mps;
  uint32_t last_command_time_ms;
} diff_drive_state_t;

typedef struct {
  uint32_t timestamp_ms;
  float linear_velocity;  // m/s
  float angular_velocity; // rad/s
  float left_velocity;    // m/s
  float right_velocity;   // m/s
} drive_state_t;

typedef void (*motor_monitor_fn_t)(const diff_drive_state_t *state);

void motor_init(void);
bool motor_set(float left_mps, float right_mps);
void motor_get(float *left_mps, float *right_mps);
void motor_set_monitor_callback(motor_monitor_fn_t monitor);

#ifdef UNIT_TEST
void motor_test_reset(void);
#endif

#endif
