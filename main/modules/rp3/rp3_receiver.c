#include "rp3_receiver.h"

#include "config.h"
#include "diag.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "spinlock.h"
#include <string.h>

static const char *TAG = "rp3_receiver";

#define RP3_UART_PORT UART_NUM_2
#define RP3_UART_BAUD_RATE 420000
#define RP3_UART_RX_BUFFER_SIZE 256
#define RP3_JOB_STACK_SIZE 4096
#define RP3_JOB_PRIORITY 5

#define CRSF_FRAME_MAX_SIZE 64
#define CRSF_FRAME_MIN_PAYLOAD_SIZE 2

typedef enum {
  CRSF_FRAME_TYPE_LINK_STATS = 0x14,
  CRSF_FRAME_TYPE_RC_CHANNELS = 0x16,
} crsf_frame_type_t;

static rp3_receiver_t rp3_receiver;
static rp3_monitor_fn_t rp3_monitor;

static uint8_t crsf_crc8(const uint8_t *data, size_t len)
{
  uint8_t crc = 0;

  while (len--) {
    crc ^= *data++;
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? ((crc << 1) ^ 0xD5) : (crc << 1);
    }
  }

  return crc;
}

static void rp3_store_signal_sample(const rp3_signal_sample_t *sample)
{
  rp3_signal_sample_t enriched_sample = *sample;

  taskENTER_CRITICAL(&rp3_receiver.lock);
  if (rp3_receiver.rc_channels_valid) {
    enriched_sample.rc_channels_valid = true;
    memcpy(enriched_sample.rc_channels, rp3_receiver.rc_channels, sizeof(rp3_receiver.rc_channels));
  }
  rp3_receiver.latest_signal = enriched_sample;
  rp3_receiver.link_stats_valid = true;
  rp3_receiver.signal_history[rp3_receiver.signal_history_head] = enriched_sample;
  rp3_receiver.signal_history_head = (rp3_receiver.signal_history_head + 1) % RP3_SIGNAL_HISTORY_LEN;
  if (rp3_receiver.signal_history_count < RP3_SIGNAL_HISTORY_LEN) {
    rp3_receiver.signal_history_count++;
  }
  taskEXIT_CRITICAL(&rp3_receiver.lock);
}

static void rp3_decode_link_statistics(const uint8_t *payload, size_t payload_len)
{
  if (payload_len < 10) {
    return;
  }

  rp3_signal_sample_t sample = {
      .timestamp_us = esp_timer_get_time(),
      .uplink_rssi_dbm = -(int8_t)payload[0],
      .uplink_snr_db = (int8_t)payload[3],
      .uplink_link_quality = payload[2],
      .active_antenna = payload[4],
      .rf_mode = payload[5],
      .tx_power = payload[6],
      .downlink_rssi_dbm = -(int8_t)payload[7],
      .downlink_link_quality = payload[8],
      .downlink_snr_db = (int8_t)payload[9],
  };

  rp3_store_signal_sample(&sample);
  taskENTER_CRITICAL(&rp3_receiver.lock);
  sample = rp3_receiver.latest_signal;
  taskEXIT_CRITICAL(&rp3_receiver.lock);
  diag_log_rp3(&sample);
  if (rp3_monitor != NULL)
    rp3_monitor(&sample);
}

static void rp3_decode_rc_channels(const uint8_t *payload, size_t payload_len)
{
  if (payload_len < 22) {
    return;
  }

  uint32_t bit_buffer = 0;
  int bits_in_buffer = 0;
  size_t channel_index = 0;
  uint16_t channels[16] = {0};

  for (size_t i = 0; i < payload_len && channel_index < 16; i++) {
    bit_buffer |= ((uint32_t)payload[i]) << bits_in_buffer;
    bits_in_buffer += 8;

    while (bits_in_buffer >= 11 && channel_index < 16) {
      channels[channel_index++] = bit_buffer & 0x07FF;
      bit_buffer >>= 11;
      bits_in_buffer -= 11;
    }
  }

  if (channel_index == 16) {
    taskENTER_CRITICAL(&rp3_receiver.lock);
    for (size_t i = 0; i < 16; i++) {
      rp3_receiver.rc_channels[i] = channels[i];
    }
    rp3_receiver.rc_channels_valid = true;

    rp3_signal_sample_t diag_sample = rp3_receiver.latest_signal;
    diag_sample.timestamp_us = esp_timer_get_time();
    diag_sample.rc_channels_valid = true;
    memcpy(diag_sample.rc_channels, channels, sizeof(channels));
    taskEXIT_CRITICAL(&rp3_receiver.lock);

    diag_log_rp3(&diag_sample);
    if (rp3_monitor != NULL)
      rp3_monitor(&diag_sample);
  }
}

static void rp3_process_crsf_frame(const uint8_t *frame, size_t frame_len)
{
  if (frame_len < 4) {
    return;
  }

  const uint8_t frame_size = frame[1];
  if (frame_len != (size_t)frame_size + 2 || frame_size < CRSF_FRAME_MIN_PAYLOAD_SIZE) {
    return;
  }

  const uint8_t expected_crc = crsf_crc8(&frame[2], frame_size - 1);
  if (expected_crc != frame[frame_len - 1]) {
    return;
  }

  const uint8_t frame_type = frame[2];
  const uint8_t *payload = &frame[3];
  const size_t payload_len = frame_size - 2;

  switch (frame_type) {
  case CRSF_FRAME_TYPE_LINK_STATS:
    rp3_decode_link_statistics(payload, payload_len);
    break;
  case CRSF_FRAME_TYPE_RC_CHANNELS:
    rp3_decode_rc_channels(payload, payload_len);
    break;
  default:
    break;
  }
}

static void rp3_uart_init(void)
{
  const uart_config_t uart_config = {
      .baud_rate = RP3_UART_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_ERROR_CHECK(uart_driver_install(RP3_UART_PORT, RP3_UART_RX_BUFFER_SIZE, 0, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(RP3_UART_PORT, &uart_config));
  ESP_ERROR_CHECK(
      uart_set_pin(RP3_UART_PORT, UART_PIN_NO_CHANGE, RP3_UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void rp3_update_job(void *args)
{
  uint8_t rx_buffer[64];
  uint8_t frame_buffer[CRSF_FRAME_MAX_SIZE] = {0};
  size_t frame_index = 0;

  while (1) {
    const int bytes_read = uart_read_bytes(RP3_UART_PORT, rx_buffer, sizeof(rx_buffer), pdMS_TO_TICKS(100));
    for (int i = 0; i < bytes_read; i++) {
      const uint8_t byte = rx_buffer[i];

      if (frame_index == 0) {
        frame_buffer[frame_index++] = byte;
        continue;
      }

      if (frame_index == 1) {
        if (byte < CRSF_FRAME_MIN_PAYLOAD_SIZE || (byte + 2) > CRSF_FRAME_MAX_SIZE) {
          frame_index = 0;
          continue;
        }
        frame_buffer[frame_index++] = byte;
        continue;
      }

      frame_buffer[frame_index++] = byte;
      if (frame_index >= sizeof(frame_buffer)) {
        frame_index = 0;
        continue;
      }

      if (frame_index == (size_t)frame_buffer[1] + 2) {
        rp3_process_crsf_frame(frame_buffer, frame_index);
        frame_index = 0;
      }
    }
  }
}

void rp3_receiver_init(void)
{
  memset(&rp3_receiver, 0, sizeof(rp3_receiver));
  spinlock_initialize(&rp3_receiver.lock);
  rp3_uart_init();
  xTaskCreate(rp3_update_job, "rp3_update_job", RP3_JOB_STACK_SIZE, &rp3_receiver, RP3_JOB_PRIORITY, NULL);
  ESP_LOGI(TAG, "RP3 receiver initialized: UART%d RX GPIO%d baud=%d", RP3_UART_PORT, RP3_UART_RX_GPIO,
           RP3_UART_BAUD_RATE);
}

void rp3_receiver_set_monitor(rp3_monitor_fn_t monitor) { rp3_monitor = monitor; }

void rp3_receiver_get_snapshot(rp3_receiver_snapshot_t *snapshot)
{
  taskENTER_CRITICAL(&rp3_receiver.lock);
  snapshot->link_stats_valid = rp3_receiver.link_stats_valid;
  snapshot->rc_channels_valid = rp3_receiver.rc_channels_valid;
  snapshot->latest_signal = rp3_receiver.latest_signal;
  snapshot->signal_history_head = rp3_receiver.signal_history_head;
  snapshot->signal_history_count = rp3_receiver.signal_history_count;
  for (size_t i = 0; i < RP3_SIGNAL_HISTORY_LEN; i++) {
    snapshot->signal_history[i] = rp3_receiver.signal_history[i];
  }
  for (size_t i = 0; i < 16; i++) {
    snapshot->rc_channels[i] = rp3_receiver.rc_channels[i];
  }
  taskEXIT_CRITICAL(&rp3_receiver.lock);
}
