extern "C" {
#include "battery.h"
#include "board_led.h"
#include "diag.h"
#include "diff_drive.h"
#include "flash_storage.h"
#include "navigation.h"
#include "ros2_msgs.h"
#include "rp3_receiver.h"
#include "shell_uart.h"
#include "system_log.h"
#include "usb_bridge.h"
}

extern "C" void app_main(void)
{
  system_log_init();
  diag_init();

  motor_init();

  flash_storage_init();

  battery_init();

  led_init();

  rp3_receiver_init();

  navigation_init();

  ros2_msgs_init();

  shell_uart_init();
}
