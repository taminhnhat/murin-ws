#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

esp_err_t navigation_init(void);
esp_err_t navigation_get_snapshot(navigation_snapshot_t *snapshot);
typedef void (*navigation_monitor_fn_t)(const navigation_imu_sample_t *sample);
typedef void (*navigation_telemetry_callback_t)(const navigation_imu_sample_t *sample);
void navigation_set_monitor(navigation_monitor_fn_t monitor);
void navigation_set_telemetry_callback(navigation_telemetry_callback_t callback);

#ifdef __cplusplus
}
#endif
