// This header file contains declarations for the two I2C controllers,
// shared by the VL53L4CD sensors, the display and the PCM5122.
// Author : David Haley
//
// Buses:
//   0 - I2C0 on GPIO0 (SDA) / GPIO1 (SCL), physical header pins 1 and 2
//   1 - I2C1 on GPIO2 (SDA) / GPIO3 (SCL), physical header pins 4 and 5

#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/i2c.h"

// Per-transfer timeout, so a bus held low returns an error instead of
// blocking forever. A 64 byte transfer at 100 kHz takes about 6 ms.
#define I2C_Bus_Timeout_US 20000

// Clears a stuck bus (see i2c_bus.c), then initialises the controller and
// its pins. Returns false if bus is not 0 or 1.
bool I2C_Bus_Init(uint8_t bus, uint16_t speed_khz);

// The controller for bus, or NULL if bus is not 0 or 1.
i2c_inst_t *I2C_Bus_Instance(uint8_t bus);

#endif
