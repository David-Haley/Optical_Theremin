// This header file contains declarations for the waveform
// selection inputs. Each input is active low with the internal
// pull up enabled; grounding a pin selects its waveform.
// Author : David Haley

#ifndef WAVEFORM_SELECT_H
#define WAVEFORM_SELECT_H

#include "dds_generator.h"

// Pico GPIO numbers (physical pin numbers in the comments).
#define Sine_Pin     10 // pin 14
#define Sine_2_Pin   11 // pin 15
#define Triangle_Pin 12 // pin 16
#define Square_Pin   13 // pin 17

// Configures the selection pins as inputs with pull ups and takes
// an initial reading, so a selection already made at power up
// applies immediately.
void Waveform_Select_Init (void);

// Returns the debounced waveform selection. The first pin, in the
// order above, that reads low determines the waveform; if no pin is
// low the waveform is Sine. A change is only accepted after two
// consecutive identical readings, so calls must be at least 20 ms
// apart.
Waveforms Waveform_Select_Read (void);

#endif // WAVEFORM_SELECT_H
