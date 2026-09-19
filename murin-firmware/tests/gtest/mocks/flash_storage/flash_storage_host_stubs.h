#pragma once

#include "esp_err.h"

/* Faults default to ESP_OK. Counters expose recovery and handle cleanup. */
typedef struct {
  esp_err_t init_results[2];
  esp_err_t erase_result;
  esp_err_t open_result;
  esp_err_t set_result;
  esp_err_t commit_result;
  unsigned int init_calls;
  unsigned int erase_calls;
  unsigned int open_calls;
  unsigned int close_calls;
  unsigned int commit_calls;
} flash_storage_host_state_t;

extern flash_storage_host_state_t flash_storage_host;
void flash_storage_host_reset(void);
