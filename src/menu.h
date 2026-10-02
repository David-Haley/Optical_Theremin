// This header file contains declarations for the menu, where the
// backlight brightness and waveform are set before playing. The
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
	/* Future: range */
};

#define DEFAULT_SETTINGS { .waveform = Sine, .brightness_level = Default_Brightness_Level }

// Indexed by Waveforms.
extern const char *const Waveform_Names [];

// Returns the backlight PWM value for brightness Level, 1 .. 16, each
// level a half stop (x 1.41) brighter than the one below.
uint8_t Brightness_PWM (int Level);

// Mutes the audio and runs the menu until Play is selected. The
// menu starts with Play selected.
void Menu_Run (struct settings *Settings);

#endif // MENU_H
