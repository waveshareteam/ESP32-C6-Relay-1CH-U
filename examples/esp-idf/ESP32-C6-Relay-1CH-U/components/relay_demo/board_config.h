#pragma once

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"

#if !CONFIG_IDF_TARGET_ESP32C6
#error "This package must be built with: idf.py set-target esp32c6"
#endif

#define BOARD_NAME                  "ESP32-C6-Relay-1CH-U"
#define RELAY_GPIO                  GPIO_NUM_15

#define RS485_UART_PORT             UART_NUM_1
#define RS485_TX_GPIO               GPIO_NUM_3
#define RS485_RX_GPIO               GPIO_NUM_2
#define RS485_DIR_GPIO              GPIO_NUM_20
#define RS485_BAUD_RATE             9600

#define RTC_I2C_PORT                I2C_NUM_0
#define RTC_I2C_SDA_GPIO            GPIO_NUM_4
#define RTC_I2C_SCL_GPIO            GPIO_NUM_5
#define RTC_I2C_FREQ_HZ             100000
#define PCF85063_I2C_ADDRESS        0x51
