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

// Lowest to highest, so turning clockwise raises the range.
static const struct {
  Octave_Ranges Range;
  const char *Name;
} Ranges [] = {
  {A0_A5, "A0 to A5"}, {B0_B5, "B0 to B5"}, {C1_C6, "C1 to C6"},
  {D1_D6, "D1 to D6"}, {E1_E6, "E1 to E6"}, {F1_F6, "F1 to F6"},
  {G1_G6, "G1 to G6"}, {A1_A6, "A1 to A6"}, {B1_B6, "B1 to B6"},
  {C2_C7, "C2 to C7"}, {D2_D7, "D2 to D7"}, {E2_E7, "E2 to E7"},
  {F2_F7, "F2 to F7"}, {G2_G7, "G2 to G7"}, {A2_A7, "A2 to A7"}
};

#define Range_Count ((int) (sizeof (Ranges) / sizeof (Ranges [0])))

_Static_assert (Range_Count == Octave_Range_Count,
                "Ranges does not have one row per Octave_Range");

// Returns the row of Ranges for Octave_Range, the default range's row
// if it is not found.
static int Range_Index (Octave_Ranges Octave_Range) {
  int Default_Index = 0;

  for (int R = 0; R < Range_Count; R++) {
    if (Ranges [R].Range == Octave_Range) {
      return R;
    } // Ranges [R].Range == Octave_Range
    if (Ranges [R].Range == Default_Octave_Range) {
      Default_Index = R;
    } // Ranges [R].Range == Default_Octave_Range
  } // R < Range_Count
  return Default_Index;
} // Range_Index

void Range_Name (Octave_Ranges Octave_Range, char *Text, int Size) {
  snprintf (Text, Size, "%s", Ranges [Range_Index (Octave_Range)].Name);
} // Range_Name

static void Format_Range (const struct settings *S, char *Text) {
  Range_Name (S->octave_range, Text, DFR0555_Columns);
} // Format_Range

// The ranges are ordered, so like Brightness they stop at the ends.
static void Edit_Range (struct settings *S, int Detents) {
  int Index = Range_Index (S->octave_range) + Detents;

  if (Index < 0) {
    Index = 0;
  } else if (Index > Range_Count - 1) {
    Index = Range_Count - 1;
  } // Index < 0
  S->octave_range = Ranges [Index].Range;
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
