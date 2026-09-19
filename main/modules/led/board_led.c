#include <stddef.h>

#include "board_led.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static led_strip_handle_t builtin_led;

static void led_task(void *arg)
{
  (void)arg;
  static const uint32_t colors[][3] = {
      {16, 0, 0},
      {0, 16, 0},
      {0, 0, 16},
  };
  size_t color_index = 0;

  while (1) {
    led_set(colors[color_index][0], colors[color_index][1], colors[color_index][2]);
    vTaskDelay(pdMS_TO_TICKS(1000));
    color_index = (color_index + 1) % (sizeof(colors) / sizeof(colors[0]));
  }
}

void led_set(uint32_t r, int32_t g, int32_t b)
{
  ESP_ERROR_CHECK(led_strip_set_pixel(builtin_led, 0, r, g, b));
  ESP_ERROR_CHECK(led_strip_refresh(builtin_led));
}

void led_init(void)
{
  led_strip_config_t strip_config = {
      .strip_gpio_num = BUILTIN_LED_GPIO,
      .max_leds = 1,
  };
  led_strip_rmt_config_t rmt_config = {
      .resolution_hz = 10 * 1000 * 1000,
      .flags.with_dma = false,
  };

  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &builtin_led));
  ESP_ERROR_CHECK(led_strip_clear(builtin_led));

  led_set(0, 16, 0);
  configASSERT(xTaskCreate(led_task, "led_task", 2048, NULL, 1, NULL) == pdPASS);
}
