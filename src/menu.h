// This header file contains declarations for the menu, where the
// backlight brightness, waveform and octave range are set before playing. The
// settings are not saved; each power up starts with the defaults.
// Author : David Haley

#ifndef MENU_H
#define MENU_H

#include <stdint.h>

#include "dds_generator.h"

#define Brightness_Levels        16
#define Default_Brightness_Level 8

struct settings {
	Waveforms waveform;
	int brightness_level; /* 1 .. Brightness_Levels */
	Octave_Ranges octave_range;
};

#define DEFAULT_SETTINGS { .waveform = Default_Waveform, .brightness_level = Default_Brightness_Level, \
			   .octave_range = Default_Octave_Range }

// Indexed by Waveforms.
extern const char *const Waveform_Names [];

// Returns the backlight PWM value for brightness Level, 1 .. 16, each
// level a half stop (x 1.41) brighter than the one below.
uint8_t Brightness_PWM (int Level);

// Writes the name of Octave_Range, for example "A1 to A6", to Text
// (at least DFR0555_Columns characters).
void Range_Name (Octave_Ranges Octave_Range, char *Text, int Size);

// Mutes the audio and runs the menu until Play is selected. The
// menu starts with Play selected.
void Menu_Run (struct settings *Settings);

#endif // MENU_H
