/*
 * SPDX-FileCopyrightText: 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INA219_H
#define INA219_H

#include "config.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

/* I2C Configuration */
#define INA219_I2C_MASTER_NUM I2C_NUM_0
#define INA219_I2C_MASTER_SDA_IO I2C_SDA
#define INA219_I2C_MASTER_SCL_IO I2C_SCL
#define INA219_I2C_MASTER_FREQ_HZ 100000
#define INA219_I2C_TIMEOUT_MS 1000

/* DFRobot INA219 I2C address (A0 = 1, A1 = 1). */
#define INA219_I2C_ADDR 0x45

/* INA219 register addresses. */
#define INA219_REG_CONFIG 0x00
#define INA219_REG_SHUNT_VOLTAGE 0x01
#define INA219_REG_BUS_VOLTAGE 0x02
#define INA219_REG_POWER 0x03
#define INA219_REG_CURRENT 0x04
#define INA219_REG_CALIBRATION 0x05

/* 32 V bus range, /8 shunt range, 12-bit ADC, continuous conversion. */
#define INA219_CONFIG 0x3FFF

/* DFRobot INA219 board: 10 mOhm shunt, 1 mA current LSB, 20 mW power LSB. */
#define INA219_CALIBRATION 4096
#define INA219_CURRENT_LSB_A 0.001f
#define INA219_POWER_LSB_W 0.020f

/** Raw and converted values returned by the INA219 sensor. */
typedef struct {
  float voltage;        // Voltage in volts
  float current;        // Current in amps
  float power;          // Power in watts
  uint16_t voltage_raw; // Raw INA219 bus-voltage register value
  int16_t current_raw;  // Raw INA219 current register value
} ina219_measurement_t;

/**
 * @brief Initialize the INA219 sensor.
 */
#ifdef __cplusplus
extern "C" {
#endif

void ina219_init(void);

/** Read voltage, current, power, and raw INA219 values. */
esp_err_t ina219_read_measurement(ina219_measurement_t *measurement);

/**
 * @brief Read INA219 bus voltage
 *
 * @param voltage Pointer to store voltage value in volts
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ina219_read_voltage(float *voltage);

/**
 * @brief Read INA219 current
 *
 * @param current Pointer to store current value in amps
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ina219_read_current(float *current);

/**
 * @brief Read INA219 power
 *
 * @param power Pointer to store power value in watts
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ina219_read_power(float *power);

#ifdef __cplusplus
}
#endif

#endif // INA219_H
