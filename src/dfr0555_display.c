// This file implements the DFR0555 display. Failures are reported
// on USB serial only when the state changes, and never stop the
// theremin: the display is not needed to play.
// Author : David Haley

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "dfr0555_display.h"

#define Display_I2C_Instance i2c0 // shared with the pitch VL53L0X
#define I2C_Timeout_us 20000

// AiP31068 LCD controller. Each write starts with a control byte:
// instruction or data.
#define LCD_Address     0x3E
#define LCD_Instruction 0x00
#define LCD_Data        0x40
#define LCD_Function    0x28 // 2 lines, 5 x 8 characters
#define LCD_On          0x0C // display on, cursor off, no flash
#define LCD_Entry       0x06 // increment, no display shift
#define LCD_Set_DDRAM   0x80
static const uint8_t Line_Address [DFR0555_Lines] = {0x00, 0x40};

// SN3193 LED driver.
#define LED_Address       0x6B
#define LED_Shutdown      0x00
#define LED_Shutdown_Run  0x20 // normal operation, all channels enabled
#define LED_Current       0x03
// Bits D4:D2: 000 42 mA, 001 10 mA, 010 5 mA, 011 30 mA, 1xx 17.5 mA
// (Documents/SN3193.pdf, table 6). Above about 9 mA average the LCD
// washes out, so 10 mA maximum makes the full PWM range usable.
#define LED_Current_10mA  0x04
#define LED_PWM_1         0x04 // OUT1, the backlight
#define LED_PWM_Update    0x07 // any write loads the PWM and control registers
#define LED_Control       0x1D
#define LED_OUT1_Only     0x01

static bool Last_OK = true;
// False until DFR0555_Init succeeds. If it fails (for example a NACK
// just after a reset) DFR0555_Put_Line retries it, as the LCD powers
// up with the display off and a later write alone would not show.
static bool Initialised = false;
static uint8_t Init_Brightness;

// Reports a change between working and failing.
static bool Check (bool OK, const char *Operation) {
  if (OK != Last_OK) {
    if (OK) {
      printf ("Display recovered\n");
    } else {
      printf ("Display failed: %s\n", Operation);
    } // OK
    Last_OK = OK;
  } // OK != Last_OK
  return OK;
} // Check

static bool Write (uint8_t Address, const uint8_t *Data, size_t Length) {
  return i2c_write_timeout_us (Display_I2C_Instance, Address, Data, Length,
                               false, I2C_Timeout_us) == (int) Length;
} // Write

static bool LCD_Command (uint8_t Command) {
  const uint8_t Data [2] = {LCD_Instruction, Command};

  return Write (LCD_Address, Data, 2);
} // LCD_Command

static bool LED_Write (uint8_t Register, uint8_t Value) {
  const uint8_t Data [2] = {Register, Value};

  return Write (LED_Address, Data, 2);
} // LED_Write

bool DFR0555_Init (uint8_t Brightness) {
  bool OK;

  Init_Brightness = Brightness;
  OK = LCD_Command (LCD_Function) &&
            LCD_Command (LCD_On) &&
            LCD_Command (LCD_Entry);

  if (!Check (OK, "LCD (0x3E) initialisation")) {
    return false;
  } // !Check (...)
  OK = LED_Write (LED_Shutdown, LED_Shutdown_Run) &&
       LED_Write (LED_Current, LED_Current_10mA) &&
       LED_Write (LED_PWM_1, Brightness) &&
       LED_Write (LED_Control, LED_OUT1_Only) &&
       LED_Write (LED_PWM_Update, 0);
  Initialised = Check (OK, "SN3193 (0x6B) initialisation");
  return Initialised;
} // DFR0555_Init

bool DFR0555_Set_Brightness (uint8_t Brightness) {
  Init_Brightness = Brightness;
  return Check (LED_Write (LED_PWM_1, Brightness) &&
                LED_Write (LED_PWM_Update, 0), "SN3193 brightness");
} // DFR0555_Set_Brightness

bool DFR0555_Put_Line (int Line, const char *Text) {
  uint8_t Data [DFR0555_Columns + 1];
  const size_t Length = strlen (Text);

  if (Line < 0 || Line >= DFR0555_Lines) {
    return false;
  } // Line out of range
  if (!Initialised && !DFR0555_Init (Init_Brightness)) {
    return false;
  } // !Initialised && !DFR0555_Init (Init_Brightness)
  Data [0] = LCD_Data;
  for (size_t C = 0; C < DFR0555_Columns; C++) {
    Data [C + 1] = C < Length ? (uint8_t) Text [C] : (uint8_t) ' ';
  } // C < DFR0555_Columns
  return Check (LCD_Command (LCD_Set_DDRAM | Line_Address [Line]) &&
                Write (LCD_Address, Data, sizeof (Data)), "LCD write");
} // DFR0555_Put_Line
