#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct flash_storage_host_nvs *nvs_handle_t;

#define NVS_READONLY 0
#define NVS_READWRITE 1

esp_err_t nvs_open(const char *namespace_name, int open_mode, nvs_handle_t *out_handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value);
esp_err_t nvs_get_u64(nvs_handle_t handle, const char *key, uint64_t *out_value);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value);
esp_err_t nvs_set_u64(nvs_handle_t handle, const char *key, uint64_t value);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t nvs_commit(nvs_handle_t handle);
