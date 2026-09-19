#include "uart_bridge.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ros2_msgs.h"
#include "sdkconfig.h"

#ifndef CONFIG_ROS2_TRANSPORT_UART_BAUD_RATE
#define CONFIG_ROS2_TRANSPORT_UART_BAUD_RATE 115200
#endif

#ifndef CONFIG_ROS2_TRANSPORT_UART_TX_GPIO
#define CONFIG_ROS2_TRANSPORT_UART_TX_GPIO 43
#endif

#ifndef CONFIG_ROS2_TRANSPORT_UART_RX_GPIO
#define CONFIG_ROS2_TRANSPORT_UART_RX_GPIO 44
#endif

#define UART_BRIDGE_PORT UART_NUM_1
#define UART_BRIDGE_TX_GPIO CONFIG_ROS2_TRANSPORT_UART_TX_GPIO
#define UART_BRIDGE_RX_GPIO CONFIG_ROS2_TRANSPORT_UART_RX_GPIO
#define UART_BRIDGE_BAUD_RATE CONFIG_ROS2_TRANSPORT_UART_BAUD_RATE
#define UART_BRIDGE_RX_BUFFER_SIZE 1024

static const char *TAG = "uart_bridge";
static ros2_msgs_ctx_t ros2_msgs;

static void uart_bridge_write_bytes(const uint8_t *data, size_t len, void *ctx)
{
  (void)ctx;
  uart_bridge_write((const char *)data, len);
}

void uart_bridge_init(void)
{
  const uart_config_t uart_config = {
      .baud_rate = UART_BRIDGE_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_LOGI(TAG, "UART bridge init port=%d tx=%d rx=%d baud=%d", UART_BRIDGE_PORT, UART_BRIDGE_TX_GPIO,
           UART_BRIDGE_RX_GPIO, UART_BRIDGE_BAUD_RATE);
  ESP_ERROR_CHECK(uart_driver_install(UART_BRIDGE_PORT, UART_BRIDGE_RX_BUFFER_SIZE, 0, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(UART_BRIDGE_PORT, &uart_config));
  ESP_ERROR_CHECK(
      uart_set_pin(UART_BRIDGE_PORT, UART_BRIDGE_TX_GPIO, UART_BRIDGE_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ros2_msgs_init(uart_bridge_write_bytes, NULL);
}

void uart_bridge_task(void *pvParameters)
{
  (void)pvParameters;

  uint8_t rx_buf[256];
  while (1) {
    const int bytes_read = uart_read_bytes(UART_BRIDGE_PORT, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(20));
    if (bytes_read > 0) {
      ros2_msgs_on_rx(rx_buf, (size_t)bytes_read);
    }
  }
}

void uart_bridge_write(const char *data, size_t len)
{
  size_t offset = 0;

  while (offset < len) {
    const int written = uart_write_bytes(UART_BRIDGE_PORT, data + offset, len - offset);
    if (written < 0) {
      ESP_LOGE(TAG, "UART write failed");
      return;
    }
    offset += (size_t)written;
  }
}
