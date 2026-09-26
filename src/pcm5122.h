// This header file contains declarations for control of the
// PCM5122 DAC (PiFi DAC+ V2.0) over I2C.
// Author : David Haley

#ifndef PCM5122_H
#define PCM5122_H

#include <stdbool.h>

// Pico GPIO numbers (physical pin numbers in the comments).
#define PCM5122_I2C_Instance i2c1
#define PCM5122_SDA_Pin      2 // pin 4
#define PCM5122_SCL_Pin      3 // pin 5
#define PCM5122_Address      0x4D

// Configures the PCM5122 for 16 bit I2S, clocked by its PLL from
// BCK (no SCK), with 0 dB digital volume, and waits for it to reach
// the Run state. The I2S stream must already be running. Returns
// false, after printing diagnostics, if configuration fails.
bool PCM5122_Init (void);

// Prints the PCM5122 clock and power state registers.
void PCM5122_Report_Status (void);

#endif // PCM5122_H
