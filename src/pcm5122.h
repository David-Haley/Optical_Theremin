// This header file contains declarations for control of the
// PCM5122 DAC (PiFi DAC+ V2.0) over I2C.
// Author : David Haley

#ifndef PCM5122_H
#define PCM5122_H

#include <stdbool.h>
#include <stdint.h>

// Pico GPIO numbers (physical pin numbers in the comments).
#define PCM5122_I2C_Instance i2c1
#define PCM5122_SDA_Pin      2 // pin 4
#define PCM5122_SCL_Pin      3 // pin 5
#define PCM5122_Address      0x4D
#define PCM5122_I2C_Speed_Hz 100000 // shared with the volume VL53L4CD

// Digital volume code (datasheet table 29): 0 is +24 dB and each step
// is -0.5 dB, so 48 is 0 dB.
#define PCM5122_Volume_0dB 48

// Configures the PCM5122 for 32 bit I2S, clocked by its PLL from
// BCK (no SCK), with the digital volume fixed at 0 dB (the volume is
// set by DDS_Generator), and waits for it to reach the Run state. The
// I2S stream must already be running. Returns false, after printing
// diagnostics, if configuration fails.
bool PCM5122_Init (void);

// Prints the I2C1 idle levels and the addresses that acknowledge a
// read (the PCM5122 at 0x4D and the volume VL53L4CD at 0x29).
void PCM5122_Scan_Bus (void);

// Prints the PCM5122 clock and power state registers.
void PCM5122_Report_Status (void);

#endif // PCM5122_H
