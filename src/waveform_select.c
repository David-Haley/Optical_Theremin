// This file implements the waveform selection inputs.
// Author : David Haley

#include "pico/stdlib.h"

#include "waveform_select.h"

// Indexed by Waveforms, in priority order.
static const uint Select_Pin [] = {Sine_Pin, Sine_2_Pin, Triangle_Pin,
                                   Square_Pin};

_Static_assert (sizeof (Select_Pin) / sizeof (Select_Pin [0]) == Square + 1,
                "Select_Pin does not have one pin per waveform");

// Current is the debounced selection, Candidate the previous raw
// reading.
static Waveforms Current = Sine;
static Waveforms Candidate = Sine;

// Returns the waveform of the first pin reading low, or Sine if none
// is low.
static Waveforms Raw_Read (void) {
  for (int W = Sine; W <= Square; W++) {
    if (!gpio_get (Select_Pin [W])) {
      return (Waveforms) W;
    } // !gpio_get (Select_Pin [W])
  } // W <= Square
  return Sine;
} // Raw_Read

void Waveform_Select_Init (void) {
  for (int W = Sine; W <= Square; W++) {
    gpio_init (Select_Pin [W]);
    gpio_set_dir (Select_Pin [W], GPIO_IN);
    gpio_pull_up (Select_Pin [W]);
  } // W <= Square
  // Allow the pull ups to charge the pin capacitance before reading.
  sleep_us (10);
  Current = Raw_Read ();
  Candidate = Current;
} // Waveform_Select_Init

Waveforms Waveform_Select_Read (void) {
  const Waveforms Reading = Raw_Read ();

  if (Reading != Current && Reading == Candidate) {
    Current = Reading;
  } // Reading != Current && Reading == Candidate
  Candidate = Reading;
  return Current;
} // Waveform_Select_Read
