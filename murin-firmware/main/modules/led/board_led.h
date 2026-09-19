#ifndef BOARD_LED_H
#define BOARD_LED_H
#include "led_strip.h"

void led_init(void);
void led_set(uint32_t r, int32_t g, int32_t b);

#endif // BOARD_LED_H
