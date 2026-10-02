// This file implements the rotary encoder and push switch inputs.
// Author : David Haley

#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "encoder.h"

// Quadrature transitions per detent: the SR1230 goes through one
// full cycle (A B = 11, 10, 00, 01, 11) per detent and rests at 11.
#define Encoder_Transitions_Per_Detent 4

// +1 or -1, so that clockwise counts up. With A on GP10 and B on
// GP11 the transition table counts clockwise down.
#define Encoder_Direction -1

// A B at rest, between detents.
#define Rest_State 3

#define Sample_Period_ms 1
// A press, and the release that must follow it, need this many
// consecutive identical samples.
#define Debounce_Samples 20

// Indexed by (previous A B << 2) | current A B. Valid Gray code
// transitions give +1 or -1. No change, and invalid transitions
// (both inputs changed, as contact bounce can cause), give 0.
static const int8_t Transition_Table [16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

static repeating_timer_t Timer;
static uint8_t Previous_State;
static int Partial_Detent = 0;
static int Push_Count = 0;
static bool Push_Down = false;

// Written by the timer callback, read and cleared by the main loop
// with interrupts disabled.
static volatile int Detents = 0;
static volatile bool Press_Pending = false;

static uint8_t Read_State (void) {
  return (uint8_t) ((gpio_get (Encoder_A_Pin) ? 2u : 0u) |
                    (gpio_get (Encoder_B_Pin) ? 1u : 0u));
} // Read_State

static bool Sample (repeating_timer_t *Unused) {
  const uint8_t State = Read_State ();
  const int Step =
    Encoder_Direction * Transition_Table [(Previous_State << 2) | State];
  const bool Pushed = !gpio_get (Encoder_Push_Pin);

  (void) Unused;
  Previous_State = State;
  // A detent is counted on arriving at the rest state, if at least
  // half a cycle was seen in one direction, and the partial count
  // restarts there. A transition missed (for example to bounce)
  // therefore can't put the count out of step with the detents.
  Partial_Detent += Step;
  if (Step != 0 && State == Rest_State) {
    if (Partial_Detent >= Encoder_Transitions_Per_Detent / 2) {
      Detents++;
    } else if (Partial_Detent <= -Encoder_Transitions_Per_Detent / 2) {
      Detents--;
    } // Partial_Detent >= Encoder_Transitions_Per_Detent / 2
    Partial_Detent = 0;
  } // Step != 0 && State == Rest_State

  // Push_Count counts consecutive samples that differ from the
  // debounced state Push_Down; enough of them change it.
  if (Pushed != Push_Down) {
    Push_Count++;
    if (Push_Count >= Debounce_Samples) {
      Push_Down = Pushed;
      Push_Count = 0;
      if (Push_Down) {
        Press_Pending = true;
      } // Push_Down
    } // Push_Count >= Debounce_Samples
  } else {
    Push_Count = 0;
  } // Pushed != Push_Down
  return true; // keep repeating
} // Sample

void Encoder_Init (void) {
  const uint Pins [] = {Encoder_A_Pin, Encoder_B_Pin, Encoder_Push_Pin};

  for (unsigned P = 0; P < sizeof (Pins) / sizeof (Pins [0]); P++) {
    gpio_init (Pins [P]);
    gpio_set_dir (Pins [P], GPIO_IN);
    gpio_pull_up (Pins [P]);
  } // P < number of pins
  // Allow the pull ups to charge the pin capacitance before reading.
  sleep_us (10);
  Previous_State = Read_State ();
  // A switch held at power up is not a press until it is released.
  Push_Down = !gpio_get (Encoder_Push_Pin);
  // Negative period: fixed rate, measured from start to start.
  add_repeating_timer_ms (-Sample_Period_ms, Sample, NULL, &Timer);
} // Encoder_Init

static int Take (volatile int *Count) {
  const uint32_t Saved = save_and_disable_interrupts ();
  const int Value = *Count;

  *Count = 0;
  restore_interrupts (Saved);
  return Value;
} // Take

int Encoder_Take_Detents (void) {
  return Take (&Detents);
} // Encoder_Take_Detents

bool Encoder_Take_Press (void) {
  const uint32_t Saved = save_and_disable_interrupts ();
  const bool Value = Press_Pending;

  Press_Pending = false;
  restore_interrupts (Saved);
  return Value;
} // Encoder_Take_Press

void Encoder_Flush (void) {
  const uint32_t Saved = save_and_disable_interrupts ();

  Detents = 0;
  Press_Pending = false;
  restore_interrupts (Saved);
} // Encoder_Flush
