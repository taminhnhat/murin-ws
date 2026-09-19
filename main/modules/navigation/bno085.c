#include "bno085.h"

#include "config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "bno085";

#define BNO085_SPI_HOST SPI2_HOST
#define BNO085_SPI_CLOCK_HZ 1000000
// Adafruit's SPI HAL allows the BNO08x up to 500 ms to assert INT before
// every read or write transaction, including the first command after boot.
#define BNO085_TIMEOUT_MS 500
#define BNO085_MAX_PACKET 512
#define BNO085_CHANNEL_EXECUTABLE 1
#define BNO085_CHANNEL_COMMAND 2
#define BNO085_CHANNEL_INPUT 3
#define BNO085_REPORT_ACCELEROMETER 0x01
#define BNO085_REPORT_GYROSCOPE 0x02
#define BNO085_REPORT_MAGNETIC_FIELD 0x03
#define BNO085_REPORT_ROTATION_VECTOR 0x05
#define BNO085_REPORT_BASE_TIMESTAMP 0xFB
#define BNO085_REPORT_TIMESTAMP_REBASE 0xFA
#define BNO085_REPORT_FLUSH_COMPLETED 0xEF
#define BNO085_REPORT_SET_FEATURE_COMMAND 0xFD

static spi_device_handle_t s_spi;
static bool s_initialized;
// SHTP sequence numbers are maintained independently for each channel.
static uint8_t s_tx_sequence[6];
static navigation_imu_sample_t s_latest_sample;
static SemaphoreHandle_t s_int_semaphore;

static int16_t read_i16(const uint8_t *p) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

static void IRAM_ATTR bno085_int_isr(void *arg)
{
  (void)arg;
  BaseType_t higher_priority_task_woken = pdFALSE;
  if (s_int_semaphore != NULL)
    xSemaphoreGiveFromISR(s_int_semaphore, &higher_priority_task_woken);
  if (higher_priority_task_woken == pdTRUE)
    portYIELD_FROM_ISR();
}

static esp_err_t wait_for_int(void)
{
  if (gpio_get_level(BNO085_INT) == 0) {
    // The edge may have already populated the semaphore while INT remains low.
    if (s_int_semaphore != NULL)
      xSemaphoreTake(s_int_semaphore, 0);
    return ESP_OK;
  }
  if (s_int_semaphore != NULL && xSemaphoreTake(s_int_semaphore, pdMS_TO_TICKS(BNO085_TIMEOUT_MS)) == pdTRUE) {
    return ESP_OK;
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t bno085_wait_for_data(uint32_t timeout_ms)
{
  if (!s_initialized || s_int_semaphore == NULL)
    return ESP_ERR_INVALID_STATE;
  if (gpio_get_level(BNO085_INT) == 0) {
    xSemaphoreTake(s_int_semaphore, 0);
    return ESP_OK;
  }
  return xSemaphoreTake(s_int_semaphore, pdMS_TO_TICKS(timeout_ms)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool bno085_data_ready(void) { return s_initialized && gpio_get_level(BNO085_INT) == 0; }

static esp_err_t spi_read_bytes(uint8_t *data, size_t len)
{
  esp_err_t ret = wait_for_int();
  if (ret != ESP_OK) {
    return ret;
  }
  spi_transaction_t transaction = {.length = len * 8, .rxlength = len * 8, .rx_buffer = data};
  return spi_device_transmit(s_spi, &transaction);
}

static esp_err_t write_packet(uint8_t channel, const uint8_t *payload, size_t payload_len)
{
  if (payload == NULL || payload_len + 4 > BNO085_MAX_PACKET) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t packet[BNO085_MAX_PACKET] = {0};
  uint16_t length = (uint16_t)(payload_len + 4);
  packet[0] = (uint8_t)length;
  packet[1] = (uint8_t)(length >> 8);
  packet[2] = channel;
  if (channel >= sizeof(s_tx_sequence)) {
    return ESP_ERR_INVALID_ARG;
  }
  packet[3] = s_tx_sequence[channel]++;
  memcpy(packet + 4, payload, payload_len);

  esp_err_t ret = wait_for_int();
  if (ret != ESP_OK) {
    return ret;
  }
  spi_transaction_t transaction = {.length = length * 8, .tx_buffer = packet};
  return spi_device_transmit(s_spi, &transaction);
}

static esp_err_t read_packet(uint8_t *packet, size_t capacity, size_t *length_out)
{
  if (packet == NULL || length_out == NULL || capacity < 4) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret = spi_read_bytes(packet, 4);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SHTP header wait/read failed (INT=%d): %s", gpio_get_level(BNO085_INT), esp_err_to_name(ret));
    return ret;
  }
  uint16_t length = (uint16_t)packet[0] | ((uint16_t)packet[1] << 8);
  length &= 0x7FFF; // SHTP continuation flag
  if (length < 4 || length > capacity) {
    ESP_LOGE(TAG, "Invalid SHTP header %02X %02X %02X %02X (INT=%d)", packet[0], packet[1], packet[2], packet[3],
             gpio_get_level(BNO085_INT));
    return ESP_ERR_INVALID_SIZE;
  }
  // A BNO08x SPI transaction always starts at the SHTP header. The first
  // four-byte transaction only discovers the packet size; the second one
  // must read the complete packet again, including its header.
  ret = spi_read_bytes(packet, length);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SHTP body wait/read failed, length=%u (INT=%d): %s", length, gpio_get_level(BNO085_INT),
             esp_err_to_name(ret));
  }
  if (ret == ESP_OK) {
    *length_out = length;
  }
  return ret;
}

static esp_err_t enable_report(uint8_t report_id, uint32_t interval_us)
{
  uint8_t command[17] = {0};
  command[0] = BNO085_REPORT_SET_FEATURE_COMMAND;
  command[1] = report_id;
  command[5] = (uint8_t)interval_us;
  command[6] = (uint8_t)(interval_us >> 8);
  command[7] = (uint8_t)(interval_us >> 16);
  command[8] = (uint8_t)(interval_us >> 24);
  return write_packet(BNO085_CHANNEL_COMMAND, command, sizeof(command));
}

static esp_err_t wait_for_boot(void)
{
  const int64_t deadline_us = esp_timer_get_time() + 500000;
  bool advertisement_seen = false;
  bool initialize_response_seen = false;

  while (esp_timer_get_time() < deadline_us) {
    if (gpio_get_level(BNO085_INT) != 0) {
      xSemaphoreTake(s_int_semaphore, pdMS_TO_TICKS(20));
      continue;
    }

    uint8_t packet[BNO085_MAX_PACKET];
    size_t length = 0;
    esp_err_t ret = read_packet(packet, sizeof(packet), &length);
    if (ret != ESP_OK)
      return ret;

    if (packet[2] == 0 && length >= 5 && packet[4] == 0)
      advertisement_seen = true;
    if (packet[2] == BNO085_CHANNEL_COMMAND && length >= 5 && packet[4] == 0xF1)
      initialize_response_seen = true;

    // The executable reset-complete notification is queued immediately after
    // the unsolicited initialize response. Leave that notification pending so
    // INT stays asserted for the first host-to-sensor transaction. Once the
    // first feature is enabled, its reports provide subsequent transaction
    // windows. Draining every startup packet here leaves the sleeping hub with
    // INT high and no way to accept the command that would wake it.
    if (advertisement_seen && initialize_response_seen)
      return ESP_OK;
  }

  ESP_LOGE(TAG, "Boot packet timeout (INT=%d, advert=%d, init=%d)", gpio_get_level(BNO085_INT), advertisement_seen,
           initialize_response_seen);
  return ESP_ERR_TIMEOUT;
}

esp_err_t bno085_init(void)
{
  if (s_initialized) {
    return ESP_OK;
  }

  gpio_config_t reset_config = {
      .pin_bit_mask = 1ULL << BNO085_RST,
      // Keep the input path enabled so startup diagnostics read the physical
      // reset-pin level rather than GPIO's output-only default of zero.
      .mode = GPIO_MODE_INPUT_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config_t int_config = {
      .pin_bit_mask = 1ULL << BNO085_INT,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };
  s_int_semaphore = xSemaphoreCreateBinary();
  if (s_int_semaphore == NULL) {
    return ESP_ERR_NO_MEM;
  }

  esp_err_t ret = gpio_config(&reset_config);
  if (ret == ESP_OK) {
    ret = gpio_config(&int_config);
  }
  if (ret != ESP_OK) {
    return ret;
  }
  ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    return ret;
  }
  ret = gpio_isr_handler_add(BNO085_INT, bno085_int_isr, NULL);
  if (ret != ESP_OK) {
    return ret;
  }

  spi_bus_config_t bus_config = {
      .mosi_io_num = BNO085_MOSI,
      .miso_io_num = BNO085_MISO,
      .sclk_io_num = BNO085_CLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = BNO085_MAX_PACKET,
  };
  ret = spi_bus_initialize(BNO085_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    return ret;
  }

  spi_device_interface_config_t device_config = {
      .clock_speed_hz = BNO085_SPI_CLOCK_HZ,
      .mode = 3,
      .spics_io_num = BNO085_CS,
      .queue_size = 1,
  };
  ret = spi_bus_add_device(BNO085_SPI_HOST, &device_config, &s_spi);
  if (ret != ESP_OK) {
    return ret;
  }

  // Match the BNO08x hardware reset sequence used by Adafruit. Starting from
  // a known high level guarantees a real falling edge even when the ESP32
  // GPIO was left low during boot.
  gpio_set_level(BNO085_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(BNO085_RST, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(BNO085_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(10));
  memset(s_tx_sequence, 0, sizeof(s_tx_sequence));
  memset(&s_latest_sample, 0, sizeof(s_latest_sample));
  s_latest_sample.quaternion[0] = 1.0f;

  // SPI startup is driven by the hardware reset. Service the advertisement
  // and unsolicited initialize response before sending SH-2 commands.
  ret = wait_for_boot();
  if (ret == ESP_OK)
    ret = enable_report(BNO085_REPORT_ACCELEROMETER, 10000);
  if (ret == ESP_OK) {
    ret = enable_report(BNO085_REPORT_GYROSCOPE, 10000);
  }
  if (ret == ESP_OK) {
    ret = enable_report(BNO085_REPORT_MAGNETIC_FIELD, 20000);
  }
  if (ret == ESP_OK) {
    ret = enable_report(BNO085_REPORT_ROTATION_VECTOR, 10000);
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "BNO085 setup failed: %s", esp_err_to_name(ret));
    return ret;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "BNO085 initialized on SPI%d", BNO085_SPI_HOST + 1);
  return ESP_OK;
}

esp_err_t bno085_process_pending(navigation_imu_sample_t *sample)
{
  if (sample == NULL)
    return ESP_ERR_INVALID_ARG;
  if (!s_initialized)
    return ESP_ERR_INVALID_STATE;
  if (!bno085_data_ready())
    return ESP_ERR_NOT_FOUND;

  bool processed_report = false;
  esp_err_t ret = ESP_OK;
  while (bno085_data_ready()) {
    uint8_t packet[BNO085_MAX_PACKET];
    size_t length = 0;
    ret = read_packet(packet, sizeof(packet), &length);
    if (ret != ESP_OK)
      return ret;
    if (length <= 4 || packet[2] != BNO085_CHANNEL_INPUT)
      continue;

    const uint8_t *payload = packet + 4;
    const size_t payload_length = length - 4;
    size_t cursor = 0;
    while (cursor < payload_length) {
      const uint8_t report_id = payload[cursor];

      // SH-2 input packets normally start with a base timestamp and may pack
      // multiple sensor reports behind it.
      if (report_id == BNO085_REPORT_BASE_TIMESTAMP || report_id == BNO085_REPORT_TIMESTAMP_REBASE) {
        if (payload_length - cursor < 5)
          break;
        cursor += 5;
        continue;
      }
      if (report_id == BNO085_REPORT_FLUSH_COMPLETED) {
        if (payload_length - cursor < 2)
          break;
        cursor += 2;
        continue;
      }

      if (report_id == BNO085_REPORT_ACCELEROMETER || report_id == BNO085_REPORT_GYROSCOPE ||
          report_id == BNO085_REPORT_MAGNETIC_FIELD) {
        const size_t report_length = 10;
        if (payload_length - cursor < report_length)
          break;
        const uint8_t *data = payload + cursor + 4;
        const float scale =
            report_id == BNO085_REPORT_ACCELEROMETER ? 256.0f : (report_id == BNO085_REPORT_GYROSCOPE ? 512.0f : 16.0f);
        float *target = report_id == BNO085_REPORT_ACCELEROMETER
                            ? s_latest_sample.acceleration_mps2
                            : (report_id == BNO085_REPORT_GYROSCOPE ? s_latest_sample.angular_velocity_rad_s
                                                                    : s_latest_sample.magnetic_field_uT);
        target[0] = read_i16(data) / scale;
        target[1] = read_i16(data + 2) / scale;
        target[2] = read_i16(data + 4) / scale;
        processed_report = true;
        cursor += report_length;
        continue;
      }

      if (report_id == BNO085_REPORT_ROTATION_VECTOR) {
        const size_t report_length = 14;
        if (payload_length - cursor < report_length)
          break;
        const uint8_t *data = payload + cursor + 4;
        s_latest_sample.quaternion[1] = read_i16(data) / 16384.0f;
        s_latest_sample.quaternion[2] = read_i16(data + 2) / 16384.0f;
        s_latest_sample.quaternion[3] = read_i16(data + 4) / 16384.0f;
        s_latest_sample.quaternion[0] = read_i16(data + 6) / 16384.0f;
        s_latest_sample.data_valid = true;
        processed_report = true;
        cursor += report_length;
        continue;
      }

      // Report lengths are advertised by SH-2. Since this compact driver only
      // enables the reports above, an unknown ID cannot be skipped safely.
      break;
    }
    if (processed_report)
      s_latest_sample.timestamp_us = esp_timer_get_time();
  }

  if (!processed_report)
    return ESP_ERR_NOT_FOUND;
  *sample = s_latest_sample;
  return ESP_OK;
}
