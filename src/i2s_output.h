// This header file contains declarations for the I2S audio
// output. Samples from DDS_Generator are streamed continuously
// to the PCM5122 DAC using PIO0 and two ping-pong DMA channels.
// Author : David Haley

#ifndef I2S_OUTPUT_H
#define I2S_OUTPUT_H

#include "dds_generator.h"

// Pico GPIO numbers (physical pin numbers in the comments).
#define I2S_DIN_Pin  26 // pin 31
#define I2S_BCK_Pin  20 // pin 26, LRCK must be BCK + 1
#define I2S_LRCK_Pin 21 // pin 27

// Fills both DMA buffers and starts the I2S stream. BCK and LRCK
// run continuously from this point, which the PCM5122 needs to
// lock its PLL, so call this before PCM5122_Init.
void I2S_Output_Start (void);

// Sets the distance in mm that determines the output frequency.
// Values outside Highest_Note_MM .. Lowest_Note_MM are clamped
// by DDS_Generator.
void I2S_Output_Set_Distance (int Distance);

// Sets the waveform produced by DDS_Generator. The phase is
// continuous across a change so switching does not click.
void I2S_Output_Set_Waveform (Waveforms Waveform);

// Prints the PIO state machine and DMA state and, for each I2S pin,
// the transitions driven by the PIO against those read back from the
// pin, for diagnosing a PCM5122 that fails to lock. Expect about 2800
// (BCK) and 88 (LRCK) transitions in 1 ms; driven transitions with
// none read back mean the pin is being held by something external.
void I2S_Output_Report_Status (void);

#endif // I2S_OUTPUT_H
