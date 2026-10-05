/*
 * Raspberry Pi Pico platform layer for the ST VL53L4CD ULD driver,
 * replacing the empty template in STSW-IMG026 (Platform/platform.h).
 * Author : David Haley
 */

#ifndef _PLATFORM_H_
#define _PLATFORM_H_
#pragma once

#include <stdint.h>
#include <string.h>

/*
 * VL53L4CD device instance. The ULD passes it by value and normally holds
 * just the 8-bit I2C address (0x52). Both sensors are at that address on
 * different controllers, so the high byte holds the bus (see i2c_bus.h).
 */
typedef uint16_t Dev_t;

#define VL53L4CD_DEFAULT_ADDRESS 0x52 /* 8 bit; 0x29 as a 7-bit address */
#define VL53L4CD_Dev(bus) ((Dev_t)(((bus) << 8) | VL53L4CD_DEFAULT_ADDRESS))

/**
 * @brief Error instance.
 */
typedef uint8_t VL53L4CD_Error;

/**
 * @brief If the macro below is defined, the device will be programmed to run
 * with I2C Fast Mode Plus (up to 1MHz). Otherwise, default max value is 400kHz.
 */

//#define VL53L4CD_I2C_FAST_MODE_PLUS

/* Register access, 16-bit index and data both big-endian. Return 0 if OK. */
uint8_t VL53L4CD_RdDWord(Dev_t dev, uint16_t registerAddr, uint32_t *value);
uint8_t VL53L4CD_RdWord(Dev_t dev, uint16_t registerAddr, uint16_t *value);
uint8_t VL53L4CD_RdByte(Dev_t dev, uint16_t registerAddr, uint8_t *value);
uint8_t VL53L4CD_WrByte(Dev_t dev, uint16_t RegisterAdress, uint8_t value);
uint8_t VL53L4CD_WrWord(Dev_t dev, uint16_t RegisterAdress, uint16_t value);
uint8_t VL53L4CD_WrDWord(Dev_t dev, uint16_t RegisterAdress, uint32_t value);

/* Waits TimeMs milliseconds. */
uint8_t VL53L4CD_WaitMs(Dev_t dev, uint32_t TimeMs);

#endif	// _PLATFORM_H_
