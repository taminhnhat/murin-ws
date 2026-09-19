/*
 * SPDX-FileCopyrightText: 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ina219.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ina219";

static i2c_master_bus_handle_t s_ina219_i2c_bus;
static i2c_master_dev_handle_t s_ina219_dev;
static bool s_ina219_initialized;
static bool s_ina219_available;

static void ina219_cleanup(void)
{
  if (s_ina219_dev != NULL) {
    i2c_master_bus_rm_device(s_ina219_dev);
    s_ina219_dev = NULL;
  }
  if (s_ina219_i2c_bus != NULL) {
    i2c_del_master_bus(s_ina219_i2c_bus);
    s_ina219_i2c_bus = NULL;
  }

  s_ina219_initialized = false;
  s_ina219_available = false;
}

/**
 * @brief Read a big-endian 16-bit register from the INA219
 *
 * @param reg_addr Register address
 * @param data Pointer to store 16-bit value
 * @return esp_err_t ESP_OK on success
 */
static esp_err_t i2c_read_register_16(uint8_t reg_addr, uint16_t *data)
{
  if (data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_ina219_available || s_ina219_dev == NULL) {
    return ESP_ERR_NOT_FOUND;
  }

  uint8_t reg_data[2] = {0};
  esp_err_t ret = i2c_master_transmit_receive(s_ina219_dev, &reg_addr, sizeof(reg_addr), reg_data, sizeof(reg_data),
                                              INA219_I2C_TIMEOUT_MS);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read register 0x%02X: %s", reg_addr, esp_err_to_name(ret));
    return ret;
  }

  *data = ((uint16_t)reg_data[0] << 8) | reg_data[1];
  return ESP_OK;
}

/**
 * @brief Write a big-endian 16-bit register to the INA219
 */
static esp_err_t i2c_write_register_16(uint8_t reg_addr, uint16_t value)
{
  if (!s_ina219_available || s_ina219_dev == NULL) {
    return ESP_ERR_NOT_FOUND;
  }

  uint8_t reg_data[3] = {
      reg_addr,
      (uint8_t)(value >> 8),
      (uint8_t)value,
  };
  return i2c_master_transmit(s_ina219_dev, reg_data, sizeof(reg_data), INA219_I2C_TIMEOUT_MS);
}

/**
 * @brief Convert the INA219 bus-voltage register to volts
 *
 * @param raw_register Raw INA219 register value
 * @return float Voltage in volts
 */
static float convert_voltage(uint16_t raw_register) { return (float)(raw_register >> 3) * 0.004f; }

/**
 * @brief Convert the calibrated INA219 current register to amps
 *
 * @param raw_register Raw INA219 register value
 * @return float Current in amps
 */
static float convert_current(int16_t raw_register) { return (float)raw_register * INA219_CURRENT_LSB_A; }

/**
 * @brief Convert the INA219 power register to watts
 *
 * @param raw_register Raw INA219 register value
 * @return float Power in watts
 */
static float convert_power(uint16_t raw_register) { return (float)raw_register * INA219_POWER_LSB_W; }

void ina219_init(void)
{
  if (s_ina219_initialized) {
    return;
  }

  ESP_LOGI(TAG, "Initializing I2C for INA219 monitoring (SDA=%d, SCL=%d)", INA219_I2C_MASTER_SDA_IO,
           INA219_I2C_MASTER_SCL_IO);

  i2c_master_bus_config_t bus_config = {
      .i2c_port = INA219_I2C_MASTER_NUM,
      .sda_io_num = INA219_I2C_MASTER_SDA_IO,
      .scl_io_num = INA219_I2C_MASTER_SCL_IO,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };

  esp_err_t ret = i2c_new_master_bus(&bus_config, &s_ina219_i2c_bus);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(ret));
    ina219_cleanup();
    s_ina219_initialized = true;
    return;
  }

  ret = i2c_master_probe(s_ina219_i2c_bus, INA219_I2C_ADDR, INA219_I2C_TIMEOUT_MS);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG,
             "INA219 not found at I2C address 0x%02X, INA219 telemetry "
             "disabled: %s",
             INA219_I2C_ADDR, esp_err_to_name(ret));
    ina219_cleanup();
    s_ina219_initialized = true;
    return;
  }

  i2c_device_config_t dev_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = INA219_I2C_ADDR,
      .scl_speed_hz = INA219_I2C_MASTER_FREQ_HZ,
      .scl_wait_us = 0,
  };

  ret = i2c_master_bus_add_device(s_ina219_i2c_bus, &dev_config, &s_ina219_dev);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add INA219 device to I2C bus: %s", esp_err_to_name(ret));
    ina219_cleanup();
    s_ina219_initialized = true;
    return;
  }

  s_ina219_available = true;
  ret = i2c_write_register_16(INA219_REG_CONFIG, INA219_CONFIG);
  if (ret == ESP_OK) {
    ret = i2c_write_register_16(INA219_REG_CALIBRATION, INA219_CALIBRATION);
  }
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "INA219 configuration failed, telemetry disabled: %s", esp_err_to_name(ret));
    ina219_cleanup();
    s_ina219_initialized = true;
    return;
  }

  s_ina219_initialized = true;
  ESP_LOGI(TAG, "INA219 monitoring initialized successfully");
}

esp_err_t ina219_read_measurement(ina219_measurement_t *measurement)
{
  if (measurement == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret = ESP_OK;
  uint16_t raw_value = 0;
  int16_t raw_current = 0;

  memset(measurement, 0, sizeof(*measurement));

  if (!s_ina219_available) {
    return ESP_ERR_NOT_FOUND;
  }

  ret = i2c_read_register_16(INA219_REG_BUS_VOLTAGE, &raw_value);
  if (ret == ESP_OK) {
    /* INA219 bus-voltage data is left-aligned in bits 15:3.
     * Keep the register value for diagnostics, but expose volts to
     * callers through measurement->voltage. */
    measurement->voltage_raw = raw_value;
    measurement->voltage = convert_voltage(raw_value);
  } else {
    ESP_LOGW(TAG, "Failed to read voltage: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = i2c_read_register_16(INA219_REG_CURRENT, &raw_value);
  if (ret == ESP_OK) {
    raw_current = (int16_t)raw_value;
    measurement->current = convert_current(raw_current);
  } else {
    ESP_LOGW(TAG, "Failed to read current: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = i2c_read_register_16(INA219_REG_POWER, &raw_value);
  if (ret == ESP_OK) {
    measurement->power = convert_power(raw_value);
  } else {
    ESP_LOGW(TAG, "Failed to read power: %s", esp_err_to_name(ret));
    return ret;
  }

  measurement->current_raw = raw_current;
  return ESP_OK;
}

esp_err_t ina219_read_voltage(float *voltage)
{
  if (voltage == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint16_t raw_value = 0;
  esp_err_t ret = i2c_read_register_16(INA219_REG_BUS_VOLTAGE, &raw_value);
  if (ret == ESP_OK) {
    *voltage = convert_voltage(raw_value);
  }
  return ret;
}

esp_err_t ina219_read_current(float *current)
{
  if (current == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint16_t raw_value = 0;
  esp_err_t ret = i2c_read_register_16(INA219_REG_CURRENT, &raw_value);
  if (ret == ESP_OK) {
    *current = convert_current((int16_t)raw_value);
  }
  return ret;
}

esp_err_t ina219_read_power(float *power)
{
  if (power == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint16_t raw_value = 0;
  esp_err_t ret = i2c_read_register_16(INA219_REG_POWER, &raw_value);
  if (ret == ESP_OK) {
    *power = convert_power(raw_value);
  }
  return ret;
}
