#pragma once

#include "esp_err.h"
#include "navigation.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bno085_init(void);
esp_err_t bno085_wait_for_data(uint32_t timeout_ms);
esp_err_t bno085_process_pending(navigation_imu_sample_t *sample);
bool bno085_data_ready(void);

#ifdef __cplusplus
}
#endif
