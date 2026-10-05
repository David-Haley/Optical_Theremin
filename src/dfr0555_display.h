// This header file contains declarations for the DFRobot DFR0555
// 2 x 16 character display, version 1.1: an AiP31068 LCD controller
// and an SN3193 backlight driver (only OUT1 used), both on I2C0
// (pins 1 and 2), shared with the pitch VL53L4CD. Ported from the
// Ada package DFR0555_Display (Pi_Common). I2C0 must already be
// initialised (I2C_Bus_Init) before any call.
// Author : David Haley

#ifndef DFR0555_DISPLAY_H
#define DFR0555_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#define DFR0555_Lines   2
#define DFR0555_Columns 16

// Initialises the LCD (display on, no cursor) and turns the
// backlight on at PWM value Brightness (0 .. 255).
bool DFR0555_Init (uint8_t Brightness);

// Sets the backlight PWM value (0 .. 255).
bool DFR0555_Set_Brightness (uint8_t Brightness);

// Writes Text to Line (0 or 1), truncated or padded with spaces to
// DFR0555_Columns characters, so the whole line is replaced.
bool DFR0555_Put_Line (int Line, const char *Text);

#endif // DFR0555_DISPLAY_H
