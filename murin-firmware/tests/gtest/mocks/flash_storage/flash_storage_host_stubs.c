#include "flash_storage_host_stubs.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct flash_storage_host_nvs {
  int open_mode;
};

flash_storage_host_state_t flash_storage_host;

void flash_storage_host_reset(void) { memset(&flash_storage_host, 0, sizeof(flash_storage_host)); }

static const char *storage_path(void) { return "flash_storage_gtest.txt"; }

static int hex_value(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static esp_err_t find_value(const char *key, char *type, char *value, size_t value_size)
{
  FILE *file = fopen(storage_path(), "r");
  char line[2048];
  if (file == NULL)
    return ESP_ERR_NVS_NOT_FOUND;

  while (fgets(line, sizeof(line), file) != NULL) {
    char line_key[128];
    char line_type;
    char line_value[1800];
    if (sscanf(line, "%127[^|]|%c|%1799s", line_key, &line_type, line_value) == 3 && strcmp(line_key, key) == 0) {
      fclose(file);
      if (strlen(line_value) + 1 > value_size)
        return ESP_ERR_INVALID_SIZE;
      *type = line_type;
      strcpy(value, line_value);
      return ESP_OK;
    }
  }
  fclose(file);
  return ESP_ERR_NVS_NOT_FOUND;
}

static esp_err_t decode(const char *key, char expected_type, uint8_t *output, size_t output_size, size_t *actual_size)
{
  char type;
  char value[1800];
  esp_err_t err = find_value(key, &type, value, sizeof(value));
  if (err != ESP_OK)
    return err;
  if (type != expected_type)
    return ESP_ERR_NVS_TYPE_MISMATCH;
  const size_t hex_length = strlen(value);
  if ((hex_length & 1U) != 0 || hex_length / 2 > output_size)
    return ESP_ERR_INVALID_SIZE;
  for (size_t i = 0; i < hex_length / 2; ++i) {
    const int high = hex_value(value[i * 2]);
    const int low = hex_value(value[i * 2 + 1]);
    if (high < 0 || low < 0)
      return ESP_FAIL;
    output[i] = (uint8_t)((high << 4) | low);
  }
  if (actual_size != NULL)
    *actual_size = hex_length / 2;
  return ESP_OK;
}

static esp_err_t encode(const char *key, char type, const uint8_t *data, size_t length)
{
  if (flash_storage_host.set_result != ESP_OK)
    return flash_storage_host.set_result;
  char lines[8192] = {0};
  FILE *input = fopen(storage_path(), "r");
  if (input != NULL) {
    fread(lines, 1, sizeof(lines) - 1, input);
    fclose(input);
  }

  char replacement[3700];
  size_t offset = 0;
  offset += (size_t)snprintf(replacement + offset, sizeof(replacement) - offset, "%s|%c|", key, type);
  for (size_t i = 0; i < length && offset + 2 < sizeof(replacement); ++i)
    offset += (size_t)snprintf(replacement + offset, sizeof(replacement) - offset, "%02X", data[i]);
  if (offset + 2 >= sizeof(replacement))
    return ESP_ERR_INVALID_SIZE;
  replacement[offset++] = '\n';
  replacement[offset] = '\0';

  char output[12000] = {0};
  char *cursor = lines;
  size_t output_length = 0;
  bool replaced = false;
  while (*cursor != '\0') {
    char *end = strchr(cursor, '\n');
    size_t line_length = end == NULL ? strlen(cursor) : (size_t)(end - cursor + 1);
    char line_key[128];
    if (sscanf(cursor, "%127[^|]|", line_key) == 1 && strcmp(line_key, key) == 0) {
      memcpy(output + output_length, replacement, offset);
      output_length += offset;
      replaced = true;
    } else {
      memcpy(output + output_length, cursor, line_length);
      output_length += line_length;
    }
    cursor += line_length;
  }
  if (!replaced) {
    memcpy(output + output_length, replacement, offset);
    output_length += offset;
  }

  FILE *file = fopen(storage_path(), "w");
  if (file == NULL)
    return ESP_FAIL;
  const size_t written = fwrite(output, 1, output_length, file);
  fclose(file);
  return written == output_length ? ESP_OK : ESP_FAIL;
}

esp_err_t nvs_open(const char *namespace_name, int open_mode, nvs_handle_t *out_handle)
{
  ++flash_storage_host.open_calls;
  if (flash_storage_host.open_result != ESP_OK)
    return flash_storage_host.open_result;
  (void)namespace_name;
  if (out_handle == NULL)
    return ESP_ERR_INVALID_ARG;
  struct flash_storage_host_nvs *handle = (struct flash_storage_host_nvs *)malloc(sizeof(*handle));
  if (handle == NULL)
    return ESP_FAIL;
  handle->open_mode = open_mode;
  *out_handle = handle;
  return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
  ++flash_storage_host.close_calls;
  free(handle);
}

esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value)
{
  (void)handle;
  return decode(key, 'u', out_value, sizeof(*out_value), NULL);
}

esp_err_t nvs_get_u64(nvs_handle_t handle, const char *key, uint64_t *out_value)
{
  (void)handle;
  return decode(key, 'q', (uint8_t *)out_value, sizeof(*out_value), NULL);
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length)
{
  (void)handle;
  if (length == NULL)
    return ESP_ERR_INVALID_ARG;
  size_t actual_size = 0;
  esp_err_t err = decode(key, 'b', (uint8_t *)out_value, *length, &actual_size);
  if (err == ESP_OK)
    *length = actual_size;
  return err;
}

esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
  return handle == NULL || handle->open_mode != NVS_READWRITE ? ESP_ERR_INVALID_ARG : encode(key, 'u', &value, 1);
}

esp_err_t nvs_set_u64(nvs_handle_t handle, const char *key, uint64_t value)
{
  return handle == NULL || handle->open_mode != NVS_READWRITE
             ? ESP_ERR_INVALID_ARG
             : encode(key, 'q', (const uint8_t *)&value, sizeof(value));
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length)
{
  return handle == NULL || handle->open_mode != NVS_READWRITE ? ESP_ERR_INVALID_ARG
                                                              : encode(key, 'b', (const uint8_t *)value, length);
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
  ++flash_storage_host.commit_calls;
  return handle == NULL ? ESP_ERR_INVALID_ARG : flash_storage_host.commit_result;
}

esp_err_t nvs_flash_init(void)
{
  const unsigned int call = flash_storage_host.init_calls++;
  return flash_storage_host.init_results[call == 0 ? 0 : 1];
}

esp_err_t nvs_flash_erase(void)
{
  ++flash_storage_host.erase_calls;
  if (flash_storage_host.erase_result != ESP_OK)
    return flash_storage_host.erase_result;
  remove(storage_path());
  return ESP_OK;
}
