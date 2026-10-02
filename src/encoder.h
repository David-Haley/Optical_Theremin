// This header file contains declarations for the rotary encoder
// (SR1230, 30 detents per rotation) and its push switch. The A, B
// and push inputs are active low with the internal pull ups
// enabled. They are sampled every 1 ms from a repeating timer, so
// that a fast turn is not missed while the main loop is busy, for
// example writing to the display.
// Author : David Haley

#ifndef ENCODER_H
#define ENCODER_H

#include <stdbool.h>

// Pico GPIO numbers (physical pin numbers in the comments).
#define Encoder_A_Pin    10 // pin 14
#define Encoder_B_Pin    11 // pin 15
#define Encoder_Push_Pin 12 // pin 16

// Configures the inputs and starts sampling them.
void Encoder_Init (void);

// Returns the number of detents turned since the last call,
// positive clockwise, and clears it.
int Encoder_Take_Detents (void);

// Returns true, once, for each debounced press of the push switch.
bool Encoder_Take_Press (void);

// Discards any detents and press not yet taken.
void Encoder_Flush (void);

#endif // ENCODER_H
