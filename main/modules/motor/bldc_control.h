/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BLDC_CONTROL_H
#define BLDC_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool valid;
  uint32_t timestamp_ms;
  uint32_t update_count;
  int32_t total_counts[NUM_MOTORS];
  int32_t delta_counts[NUM_MOTORS];
  float velocity_rps[NUM_MOTORS];
  float velocity_rpm[NUM_MOTORS];
} bldc_encoder_snapshot_t;

typedef struct {
  pcnt_unit_handle_t encoders[NUM_MOTORS];
  ledc_channel_t pwm_channels[NUM_MOTORS];
  uint32_t pwm_duties[NUM_MOTORS];
  int32_t last_counts[NUM_MOTORS];
  uint32_t last_update_ms;
  bldc_encoder_snapshot_t snapshot;
  bool initialized;
} bldc_control_system_t;

esp_err_t bldc_control_init(bldc_control_system_t *system);
esp_err_t bldc_control_set_drive(bldc_control_system_t *system, int16_t left, int16_t right);
esp_err_t bldc_control_update(bldc_control_system_t *system);
void bldc_control_get_snapshot(const bldc_control_system_t *system, bldc_encoder_snapshot_t *snapshot);
void bldc_encoder_loop_cb(void *args);

#ifdef __cplusplus
}
#endif

#endif // BLDC_CONTROL_H
