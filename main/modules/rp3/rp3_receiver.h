#pragma once

#include "freertos/FreeRTOS.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RP3_SIGNAL_HISTORY_LEN 64

typedef struct {
  int64_t timestamp_us;
  int8_t uplink_rssi_dbm;
  int8_t uplink_snr_db;
  uint8_t uplink_link_quality;
  uint8_t active_antenna;
  uint8_t rf_mode;
  uint8_t tx_power;
  int8_t downlink_rssi_dbm;
  uint8_t downlink_link_quality;
  int8_t downlink_snr_db;
  bool rc_channels_valid;
  uint16_t rc_channels[16];
} rp3_signal_sample_t;

typedef struct {
  bool link_stats_valid;
  bool rc_channels_valid;
  rp3_signal_sample_t latest_signal;
  rp3_signal_sample_t signal_history[RP3_SIGNAL_HISTORY_LEN];
  size_t signal_history_head;
  size_t signal_history_count;
  uint16_t rc_channels[16];
} rp3_receiver_snapshot_t;

typedef struct {
  portMUX_TYPE lock;
  bool link_stats_valid;
  bool rc_channels_valid;
  rp3_signal_sample_t latest_signal;
  rp3_signal_sample_t signal_history[RP3_SIGNAL_HISTORY_LEN];
  size_t signal_history_head;
  size_t signal_history_count;
  uint16_t rc_channels[16];
} rp3_receiver_t;

typedef void (*rp3_monitor_fn_t)(const rp3_signal_sample_t *sample);

void rp3_receiver_init(void);
void rp3_receiver_get_snapshot(rp3_receiver_snapshot_t *snapshot);
void rp3_receiver_set_monitor(rp3_monitor_fn_t monitor);

#ifdef __cplusplus
}
#endif
