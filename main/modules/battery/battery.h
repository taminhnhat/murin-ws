/* Battery management API backed by the INA219 current/voltage monitor. */
#ifndef BATTERY_H
#define BATTERY_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  float voltage;
  float current;
  float power;
  float energy; // Energy consumed since the previous battery_fetch_data() call, in Wh.
  uint32_t timestamp;
  bool valid;
} battery_data_t;

#ifdef __cplusplus
extern "C" {
#endif

void battery_init(void);
esp_err_t battery_fetch_data(battery_data_t *data);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_H
