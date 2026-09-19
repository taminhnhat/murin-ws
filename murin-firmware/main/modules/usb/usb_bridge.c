#include "usb_bridge.h"

#include <stdbool.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ros2_msgs.h"
#include "sdkconfig.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

static const char *TAG = "usb_bridge";
#define USB_BRIDGE_WRITE_RETRIES 100
static usb_bridge_cb_t rx_cb = NULL;
static volatile bool cdc_dtr = false;
static volatile bool cdc_rts = false;

size_t usb_bridge_write(const char *data, size_t len) { return usb_bridge_write_bytes((uint8_t *)data, len); }

bool usb_bridge_is_connected(void) { return cdc_dtr; }

size_t usb_bridge_write_bytes(uint8_t *data, size_t len)
{
  size_t offset = 0;

  if (!cdc_dtr) {
    return 0;
  }

  while (offset < len) {
    const size_t queued = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)data + offset, len - offset);
    if (queued == 0) {
      esp_err_t err = tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
      if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) {
        ESP_LOGD(TAG, "USB flush failed while queue is full: %s", esp_err_to_name(err));
        return offset;
      }

      bool flushed = false;
      for (int retry = 0; retry < USB_BRIDGE_WRITE_RETRIES; retry++) {
        vTaskDelay(pdMS_TO_TICKS(1));
        err = tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        if (err == ESP_OK) {
          flushed = true;
          break;
        }
        if (err != ESP_ERR_NOT_FINISHED) {
          ESP_LOGD(TAG, "USB flush failed while retrying: %s", esp_err_to_name(err));
          return offset;
        }
      }
      if (!flushed)
        return offset;
      continue;
    }

    offset += queued;
    const esp_err_t err = tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
    if (err == ESP_OK || err == ESP_ERR_NOT_FINISHED) {
      continue;
    }

    ESP_LOGD(TAG, "USB flush failed: %s", esp_err_to_name(err));
    break;
  }

  return offset;
}

size_t usb_bridge_read_bytes(uint8_t *data, size_t len)
{
  size_t rx_size = 0;
  esp_err_t err = tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, data, len, &rx_size);

  return err ? 0 : rx_size;
}

void usb_bridge_set_callback(usb_bridge_cb_t cb) { rx_cb = cb; }

static void usb_rx_cb(int itf, cdcacm_event_t *event)
{
  (void)itf;

  if (event->type == CDC_EVENT_RX) {
    if (rx_cb != NULL) {
      rx_cb();
    }
  }
}

static void usb_line_state_cb(int itf, cdcacm_event_t *event)
{
  (void)itf;

  if (event->type == CDC_EVENT_LINE_STATE_CHANGED) {
    cdc_dtr = event->line_state_changed_data.dtr;
    cdc_rts = event->line_state_changed_data.rts;
    ESP_LOGI(TAG, "USB CDC %s dtr=%d rts=%d", cdc_dtr ? "connected" : "disconnected", cdc_dtr, cdc_rts);
  }
}

void usb_bridge_init(void)
{
  ESP_LOGI(TAG, "USB initialization");
  const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
  ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

  tinyusb_config_cdcacm_t acm_cfg = {
      .cdc_port = TINYUSB_CDC_ACM_0,
      .callback_rx = usb_rx_cb,
      .callback_rx_wanted_char = NULL,
      .callback_line_state_changed = usb_line_state_cb,
      .callback_line_coding_changed = NULL,
  };

  ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg));
  ESP_LOGI(TAG, "USB initialization DONE");
}
