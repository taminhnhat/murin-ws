/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CONFIG_H
#define CONFIG_H

/* MCPWM Configuration */
#define BDC_MCPWM_TIMER_RESOLUTION_HZ 10000000 // 10MHz, 1 tick = 0.1us
#define BDC_MCPWM_FREQ_HZ 25000                // 25KHz PWM
#define BDC_MCPWM_DUTY_TICK_MAX                                                                                        \
  (BDC_MCPWM_TIMER_RESOLUTION_HZ / BDC_MCPWM_FREQ_HZ) // maximum value we can set for the duty cycle, in ticks

/* Single-pin BLDC PWM Configuration */
#define BLDC_PWM_FREQ_HZ BDC_MCPWM_FREQ_HZ
#define BLDC_PWM_DUTY_RES_BITS 10
#define BLDC_PWM_DUTY_MAX ((1U << BLDC_PWM_DUTY_RES_BITS) - 1U)
#define DIFF_DRIVE_MAX_SPEED_MPS 0.5f
#define DIFF_DRIVE_COMMAND_TIMEOUT_MS 500
#define DIFF_DRIVE_DECEL_STEP_MPS 0.05f
#define DIFF_DRIVE_STOP_EPSILON_MPS 0.01f

/* Number of motors */
#define NUM_MOTORS 4

/* Health LED */
#define BUILTIN_LED_GPIO 48

/* Encoder Configuration */
#define BDC_ENCODER_PCNT_HIGH_LIMIT 1000
#define BDC_ENCODER_PCNT_LOW_LIMIT -1000
#define WHEEL_GEAR_RATIO 51.0f
#define MOTOR_ENCODER_PULSES_PER_REV 11.0f
#define MOTOR_ENCODER_QUADRATURE_MULTIPLIER 4.0f
#define WHEEL_ENCODER_COUNTS_PER_REV 650.0f
#define BLDC_ENCODER_GLITCH_FILTER_NS 10000

/* PID Loop Configuration */
#define BDC_PID_LOOP_PERIOD_MS 10 // calculate the motor speed every 10ms
#define BDC_PID_EXPECT_SPEED 400  // expected motor speed, in the pulses counted by the rotary encoder

/* PID Controller Parameters */
#define BDC_PID_KP 0.6
#define BDC_PID_KI 0.4
#define BDC_PID_KD 0.2
#define BDC_PID_MAX_INTEGRAL 1000
#define BDC_PID_MIN_INTEGRAL -1000

/* Motor 1 GPIO pins */
#define MOT_PWM_1 2
#define MOT_PWM_2 42
#define MOT_PWM_3 41
#define MOT_PWM_4 40

#define MOT_DIR_1 39
#define MOT_DIR_2 38

#define MOT_BRAKE_1 47
#define MOT_BRAKE_2 21

#define MOT_ENC_1 4
#define MOT_ENC_2 5
#define MOT_ENC_3 6
#define MOT_ENC_4 7
#define MOT_ENC_5 15
#define MOT_ENC_6 16
#define MOT_ENC_7 17
#define MOT_ENC_8 18

/* I2C pins */
#define I2C_SDA 8
#define I2C_SCL 9

/* SPI pins */
#define SPI_CS 10
#define SPI_CLK 11
#define SPI_MOSI 12
#define SPI_MISO 13

/* RP3 UART pins */
#define RP3_UART_RX_GPIO 1

/* BNO085 pins */
#define BNO085_MOSI SPI_MOSI
#define BNO085_MISO SPI_MISO
#define BNO085_CLK SPI_CLK
#define BNO085_CS SPI_CS
#define BNO085_RST 14
#define BNO085_INT 3

#endif // CONFIG_H
