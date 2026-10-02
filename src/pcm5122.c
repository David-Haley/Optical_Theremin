// This file implements control of the PCM5122 DAC over I2C. See
// Documents/pcm5122.pdf, section 8.3.6.3 for 3-wire (BCK PLL)
// operation and section 8.6 for the register map.
// Author : David Haley

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "pcm5122.h"

#define I2C_Timeout_us 10000

// Page 0 registers
#define Page_Select      0x00
#define Reset            0x01
#define Standby          0x02
#define Mute             0x03
#define PLL              0x04
#define PLL_Reference    0x0D
#define Error_Detect     0x25
#define I2S_Format       0x28
#define Left_Volume      0x3D
#define Right_Volume     0x3E
#define Detected_FS      0x5B
#define Clock_Status     0x5E
#define Clock_Errors     0x5F
#define Power_State      0x76

#define Power_State_Mask 0x0F
#define Power_State_Run  0x05
#define Run_Timeout_ms   1000

typedef struct {
  uint8_t Register;
  uint8_t Value;
} Register_Setting;

static const Register_Setting Init_Sequence [] = {
  {Page_Select,   0x00}, // page 0
  {Standby,       0x10}, // RQST, standby
  {Reset,         0x11}, // RSTM and RSTR, reset modules and registers
  {PLL_Reference, 0x10}, // SREF = 001, PLL reference is BCK
  {Error_Detect,  0x18}, // IDSK and IDCH, no SCK is supplied
  {I2S_Format,    0x03}, // I2S, 32 bits
  {Left_Volume,   PCM5122_Volume_0dB}, // fixed, the volume is set
  {Right_Volume,  PCM5122_Volume_0dB}, // by DDS_Generator
  {Mute,          0x00}, // unmute both channels
  {Standby,       0x00}  // normal operation
};

static bool Write_Register (const uint8_t Register, const uint8_t Value) {
  const uint8_t Data [2] = {Register, Value};

  return i2c_write_timeout_us (PCM5122_I2C_Instance, PCM5122_Address, Data,
                               2, false, I2C_Timeout_us) == 2;
} // Write_Register

static bool Read_Register (const uint8_t Register, uint8_t *Value) {
  return i2c_write_timeout_us (PCM5122_I2C_Instance, PCM5122_Address,
                               &Register, 1, true, I2C_Timeout_us) == 1 &&
         i2c_read_timeout_us (PCM5122_I2C_Instance, PCM5122_Address,
                              Value, 1, false, I2C_Timeout_us) == 1;
} // Read_Register

// Prints the idle levels of SDA and SCL, which should both be high
// with pull-ups, and the result of a read at every address. Each
// address either acknowledges, is not acknowledged (NACK, or another
// abort such as lost arbitration) or times out. All NACKs indicate a
// working bus with no device responding; timeouts suggest SCL is being
// held low or the pull-ups are too weak.
void PCM5122_Scan_Bus (void) {
  uint8_t Data;
  int Result;
  int Found = 0, NACKs = 0, Timeouts = 0, Others = 0;

  printf ("I2C1 idle levels: SDA = %d, SCL = %d\n",
          gpio_get (PCM5122_SDA_Pin), gpio_get (PCM5122_SCL_Pin));
  printf ("I2C1 scan:");
  // 0x00 .. 0x07 and 0x78 .. 0x7F are reserved addresses.
  for (uint8_t Address = 0x08; Address < 0x78; Address++) {
    Result = i2c_read_timeout_us (PCM5122_I2C_Instance, Address, &Data, 1,
                                  false, I2C_Timeout_us);
    if (Result == 1) {
      printf (" 0x%02X", Address);
      Found++;
    } else if (Result == PICO_ERROR_GENERIC) {
      NACKs++;
    } else if (Result == PICO_ERROR_TIMEOUT) {
      Timeouts++;
    } else {
      Others++;
    } // Result
  } // Address < 0x78
  printf ("%s\n", Found == 0 ? " no devices found" : "");
  printf ("I2C1 scan: %d acknowledged, %d NACK/abort, %d timeout, "
          "%d other\n", Found, NACKs, Timeouts, Others);
} // Scan_Bus

void PCM5122_Report_Status (void) {
  static const uint8_t Registers [] =
    {PLL, Detected_FS, Clock_Status, Clock_Errors, Power_State};
  uint8_t Value;

  printf ("PCM5122 status:");
  for (unsigned R = 0; R < sizeof (Registers); R++) {
    if (Read_Register (Registers [R], &Value)) {
      printf (" reg %u = 0x%02X", Registers [R], Value);
    } else {
      printf (" reg %u = read failed", Registers [R]);
    } // Read_Register (Registers [R], &Value)
  } // R < sizeof (Registers)
  printf ("\n");
} // PCM5122_Report_Status

bool PCM5122_Init (void) {
  uint8_t State = 0;
  absolute_time_t Timeout;

  i2c_init (PCM5122_I2C_Instance, PCM5122_I2C_Speed_Hz);
  gpio_set_function (PCM5122_SDA_Pin, GPIO_FUNC_I2C);
  gpio_set_function (PCM5122_SCL_Pin, GPIO_FUNC_I2C);
  gpio_pull_up (PCM5122_SDA_Pin);
  gpio_pull_up (PCM5122_SCL_Pin);
  for (unsigned S = 0;
       S < sizeof (Init_Sequence) / sizeof (Init_Sequence [0]); S++) {
    if (!Write_Register (Init_Sequence [S].Register,
                         Init_Sequence [S].Value)) {
      printf ("PCM5122 write failed: reg %u = 0x%02X\n",
              Init_Sequence [S].Register, Init_Sequence [S].Value);
      PCM5122_Scan_Bus ();
      return false;
    } // !Write_Register (...)
    if (Init_Sequence [S].Register == Reset) {
      sleep_ms (10);
    } // Init_Sequence [S].Register == Reset
  } // S < Init_Sequence length
  Timeout = make_timeout_time_ms (Run_Timeout_ms);
  do {
    sleep_ms (10);
    if (Read_Register (Power_State, &State) &&
        (State & Power_State_Mask) == Power_State_Run) {
      PCM5122_Report_Status ();
      return true;
    } // Power state is Run
  } while (!time_reached (Timeout));
  printf ("PCM5122 did not reach Run state within %d ms\n",
          Run_Timeout_ms);
  PCM5122_Report_Status ();
  return false;
} // PCM5122_Init
