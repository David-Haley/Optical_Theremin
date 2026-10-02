// This file implements the menu. Turning the encoder moves between
// items; pushing selects one. Pushing Play leaves the menu. Pushing
// any other item edits it: turning changes its value and pushing
// again confirms. To add an item, add a value to struct settings and
// an entry to Items.
// Author : David Haley

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "dfr0555_display.h"
#include "encoder.h"
#include "i2s_output.h"
#include "menu.h"

#define Poll_Period_ms 10

const char *const Waveform_Names [] = {"Sine", "Sine_2", "Triangle",
                                       "Square"};

_Static_assert (sizeof (Waveform_Names) / sizeof (Waveform_Names [0]) ==
                  Square + 1,
                "Waveform_Names does not have one name per waveform");

// 255 / 1.41 ^ (16 - level), rounded.
static const uint8_t PWM_Table [Brightness_Levels] = {
  1, 2, 3, 4, 6, 8, 11, 16, 23, 32, 45, 64, 90, 128, 180, 255
};

uint8_t Brightness_PWM (int Level) {
  if (Level < 1) {
    Level = 1;
  } else if (Level > Brightness_Levels) {
    Level = Brightness_Levels;
  } // Level < 1
  return PWM_Table [Level - 1];
} // Brightness_PWM

// Returns Value + Step wrapped into 0 .. Count - 1.
static int Wrap (int Value, int Step, int Count) {
  return ((Value + Step) % Count + Count) % Count;
} // Wrap

static void Format_Brightness (const struct settings *S, char *Text) {
  snprintf (Text, DFR0555_Columns, "%d of %d", S->brightness_level,
            Brightness_Levels);
} // Format_Brightness

// Brightness stops at the ends rather than wrapping, so it can't
// jump from dimmest to brightest. The backlight follows as it is
// turned.
static void Edit_Brightness (struct settings *S, int Detents) {
  int Level = S->brightness_level + Detents;

  if (Level < 1) {
    Level = 1;
  } else if (Level > Brightness_Levels) {
    Level = Brightness_Levels;
  } // Level < 1
  if (Level != S->brightness_level) {
    S->brightness_level = Level;
    DFR0555_Set_Brightness (Brightness_PWM (Level));
  } // Level != S->brightness_level
} // Edit_Brightness

static void Format_Waveform (const struct settings *S, char *Text) {
  snprintf (Text, DFR0555_Columns, "%s", Waveform_Names [S->waveform]);
} // Format_Waveform

static void Edit_Waveform (struct settings *S, int Detents) {
  S->waveform = (Waveforms) Wrap (S->waveform, Detents, Square + 1);
} // Edit_Waveform

void Range_Name (int Octave_Shift, char *Text, int Size) {
  const int Lowest = Lowest_Note_Octave + Octave_Shift;

  snprintf (Text, Size, "A%d to A%d", Lowest, Lowest + Octave_Count);
} // Range_Name

static void Format_Range (const struct settings *S, char *Text) {
  Range_Name (S->octave_shift, Text, DFR0555_Columns);
} // Format_Range

// The ranges are ordered, so like Brightness they stop at the ends.
static void Edit_Range (struct settings *S, int Detents) {
  int Shift = S->octave_shift + Detents;

  if (Shift < Lowest_Octave_Shift) {
    Shift = Lowest_Octave_Shift;
  } else if (Shift > Highest_Octave_Shift) {
    Shift = Highest_Octave_Shift;
  } // Shift < Lowest_Octave_Shift
  S->octave_shift = Shift;
} // Edit_Range

struct menu_item {
  const char *Name;
  // Writes the value shown on line 2, NULL for no value.
  void (*Format) (const struct settings *, char *Text);
  // Changes the value by Detents, NULL if pushing leaves the menu.
  void (*Edit) (struct settings *, int Detents);
};

static const struct menu_item Items [] = {
  {"Play",       NULL,              NULL},
  {"Brightness", Format_Brightness, Edit_Brightness},
  {"Waveform",   Format_Waveform,   Edit_Waveform},
  {"Range",      Format_Range,      Edit_Range},
};

#define Item_Count ((int) (sizeof (Items) / sizeof (Items [0])))

void Menu_Run (struct settings *Settings) {
  int Item = 0; // Play
  bool Editing = false;
  bool Drawn = false;
  char Shown [DFR0555_Lines] [DFR0555_Columns + 1];

  I2S_Output_Set_Volume (Mute_MM);
  Encoder_Flush ();
  for (;;) {
    const int Detents = Encoder_Take_Detents ();
    char Value [DFR0555_Columns] = "";
    char Line [DFR0555_Lines] [DFR0555_Columns + 1];

    if (Encoder_Take_Press ()) {
      if (Editing) {
        Editing = false;
      } else if (Items [Item].Edit == NULL) {
        return;
      } else {
        Editing = true;
      } // Editing
    } // Encoder_Take_Press ()
    if (Detents != 0) {
      if (Editing) {
        Items [Item].Edit (Settings, Detents);
      } else {
        Item = Wrap (Item, Detents, Item_Count);
      } // Editing
    } // Detents != 0

    // ">" marks what turning the encoder changes: the item, or its
    // value while editing.
    if (Items [Item].Format != NULL) {
      Items [Item].Format (Settings, Value);
    } // Items [Item].Format != NULL
    snprintf (Line [0], sizeof (Line [0]), "%c%s", Editing ? ' ' : '>',
              Items [Item].Name);
    snprintf (Line [1], sizeof (Line [1]), "%c%s", Editing ? '>' : ' ',
              Value);
    for (int L = 0; L < DFR0555_Lines; L++) {
      if (!Drawn || strcmp (Line [L], Shown [L]) != 0) {
        DFR0555_Put_Line (L, Line [L]);
        strcpy (Shown [L], Line [L]);
      } // Line changed
    } // L < DFR0555_Lines
    Drawn = true;
    sleep_ms (Poll_Period_ms);
  } // for
} // Menu_Run
