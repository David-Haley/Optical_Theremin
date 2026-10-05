# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An optical theremin for a Raspberry Pi Pico W: an ST VL53L4CD time-of-flight
sensor measures hand distance (mm) over I2C, and a DDS (direct digital
synthesis) tone generator turns that distance into a tone, streamed over I2S
to a PCM5122 DAC (PiFi DAC+ V2.0).

Current state (stage 10, spec in `Documents/Stage_10.md`; earlier stages in
`Stage_2.md`–`Stage_9.md`): at boot a DFR0555 2×16 display shows the
program name and build date, then a menu driven by a rotary encoder with a
push switch sets the backlight brightness, waveform and octave range (the
five-octave span starts from any natural note A0 … A2; Sine_2 and A1 to A6
at start-up). Selecting Play
starts the theremin and a push returns to the menu (silent). Settings are
not saved. In play, two VL53L4CD
sensors (VL53L0X until stage 10) are read at 20 Hz. The new sensors are
not calibrated yet: readings are used raw. The
pitch sensor's distance sets the pitch and the volume sensor's distance sets
a gain applied inside the DDS generator; the PCM5122 volume is fixed at
0 dB. Readings are not filtered; instead the DDS generator ramps both the
pitch and the gain between readings, sample by sample. Stage 5 stepped the
PCM5122 volume over I2C instead, which crackled on fast hand movements.
What was built differs from `Stage_6.md` in places: the gain table is
indexed by distance (100–355 mm, 0.4 dB per mm), not by PCM5122 volume
code, and the wave table is 32 bit (stage 6).
USB serial carries diagnostic and error messages only — no distance readings.
The CMake target is `optical_theremin` (it was `distance_measurement`, from
the distance-measurement app the project grew from).

### Pins (Pico physical pin → GPIO)

| Signal | Pin | GPIO | Peripheral |
|---|---|---|---|
| Pitch VL53L4CD SDA / SCL | 1 / 2 | GP0 / GP1 | I2C0, addr 0x29, 100 kHz |
| DFR0555 display SDA / SCL | 1 / 2 | GP0 / GP1 | I2C0 (shared), LCD 0x3E, SN3193 backlight 0x6B |
| PCM5122 SDA / SCL | 4 / 5 | GP2 / GP3 | I2C1, addr 0x4D, 100 kHz |
| Volume VL53L4CD SDA / SCL | 4 / 5 | GP2 / GP3 | I2C1 (shared), addr 0x29 |
| I2S DIN | 31 | GP26 | PIO0 |
| I2S BCK | 26 | GP20 | PIO0 side-set |
| I2S LRCK | 27 | GP21 | PIO0 side-set (must be BCK + 1) |
| Encoder A / B | 14 / 15 | GP10 / GP11 | input, pull-up, active low |
| Encoder push | 16 | GP12 | input, pull-up, active low |

I2C0 was 400 kHz until stage 7. The display's AiP31068 LCD controller drops
characters at 400 kHz (a line showed only `>Pa`), so I2C0 runs at 100 kHz.
Stage 9 retried 400 kHz after the 3.3 V supply fix and it still failed. A
likely cause: `DFR0555_Put_Line` sends 17 bytes in one burst, and at
400 kHz a byte (~23 µs) is quicker than the LCD's character write
(~40 µs); at 100 kHz a byte takes ~90 µs.
The display is on I2C0 rather than I2C1 because that is easier to wire.

`Documents/Stage_2.md` lists the PCM5122 SDA as "pin 3"; that is the Pi
40-pin header numbering. On the Pico it is pin 4 (GP2). It also lists BCK and
LRCK on pins 32/34 (GP27/GP28); they were moved to GP20/GP21 because GP28 on
this board can no longer be driven low (it reads back high even with nothing
connected). Don't use GP28.

`instructions.txt` is the original task spec for the distance-measurement
app; the paths in it refer to the project's old location
(`/home/david/Pico_Projects/Distance_Measurement`).

## Build the Pico firmware

Environment variables (`PICO_SDK_PATH`, `FREERTOS_KERNEL_PATH`) are set by
`/home/david/pico/env.sh`, sourced automatically in interactive shells. In a
non-interactive shell, source it manually first.

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DPICOTOOL_FORCE_FETCH_FROM_GIT=1
cmake --build . -j$(nproc)
```

`-DPICOTOOL_FORCE_FETCH_FROM_GIT=1` is only needed on the first configure in a
fresh `build/` directory: the system `picotool` package (2.1.1) is older than
what this pico-sdk checkout requires (2.3.0), so CMake fetches and builds a
matching picotool from source into `build/_deps/`. Subsequent reconfigures of
the same build directory don't need the flag re-passed.

If the project directory has been moved or renamed, delete `build/` and
configure from scratch: CMake caches absolute paths, and a stale cache will
silently compile against include paths from the old location.

Output: `build/optical_theremin.uf2` and `.elf`.

The custom target `build_date` runs `build_date.cmake` on every build to
write `build/generated/build_date/build_date.h` (`Build_Date`, ISO date,
shown on the start-up screen). `file(CONFIGURE)` only rewrites it when the
date changes, so `main.c` recompiles at most once a day.

## Flash and monitor

```bash
picotool load -f -v -x build/optical_theremin.uf2
```

`-f` forces a device that's already running application code (exposing a USB
CDC serial port) to reboot into BOOTSEL mode for the load, then reboots it
back into the application afterward — no manual BOOTSEL-button/replug dance
needed.

After flashing/rebooting, the board re-enumerates and `/dev/ttyACM0` briefly
disappears and reappears. Wait for that before attaching, e.g.:

```bash
for i in $(seq 1 50); do [ -e /dev/ttyACM0 ] && break; sleep 0.1; done
timeout 20 cat /dev/ttyACM0
```

`main.c` waits 2s at boot before printing anything, which covers the
re-enumeration race. `picotool reboot -a -f` reboots the running application
without reflashing, useful for re-capturing serial output from a clean boot.

## Ada configuration tool and generated headers

`src/optical_theremin.adb` is a host-side Ada program (GNAT, built with
`optical_theremin.gpr`) that computes the DDS tables and writes them out.

```bash
gprbuild -P optical_theremin.gpr      # builds bin/optical_theremin
cd bin && ./optical_theremin           # must run from bin/: output paths are relative
```

It writes:

- `../src/dds_generator.h` — public interface: `PIO_Clock_Divisor`,
  `Sample_Rate`, `Sample_Size`, `Highest_Note_MM`, `Lowest_Note_MM`,
  `Loud_MM`, `Mute_MM`, the `Octave_Ranges` enum and `Octave_Range_Count`
  (15), the `Waveforms` enum, the `Audio_Sample` struct,
  `Default_Waveform` (`Sine_2`), `Default_Octave_Range` (`A1_A6`) and
  the `DDS_Generator()` prototype. `Sample_Rate` is a floating point
  literal; don't use it in firmware (soft float), use `PIO_Clock_Divisor`.
- `../src/dds_table.h` — `Frequency_Count`, `Phase_Step[]`,
  `Sample_Count`, `Phase_Shift`, `Wave_Table[][]`, `Volume_Count` and
  `Volume_Table[]`.
- `../Documents/Frequency.csv`, `../Documents/Sample.csv` and
  `../Documents/Volume.csv` — the same data
  for inspection/plotting (`Documents/` is git-ignored except the VL53L0X API PDF).

**`dds_generator.h` and `dds_table.h` are machine-generated and committed.**
Don't hand-edit them — change `Write_Headers` in `optical_theremin.adb`,
rebuild, rerun, and commit the `.adb` and regenerated headers together.

### DDS design (encoded in the generator)

- Sample rate 44389.2 Hz = 125 MHz / (`PIO_Clock_Divisor` 22 × 64 BCK ×
  2 PIO cycles per bit). The divider is an exact integer to avoid
  fractional-divider jitter, and the sample rate follows from it (the tool
  computes `Phase_Step[]` from it, so notes are in tune). This assumes a
  125 MHz system clock; `main.c` halts at boot if `clk_sys` differs.
- `DDS_Generator(Waveform, Pitch_Distance, Volume_Distance, Octave_Range)`
  returns one stereo `Audio_Sample` (two `int32_t`) per call.
- Pitch: 5 octaves (A1–A6, 55–1760 Hz, by default), 60 mm per octave
  (exponential in distance). Highest note at `Highest_Note_MM` (60 mm),
  lowest at `Lowest_Note_MM` (360 mm). Out-of-range distances clamp to the
  nearest end.
- Range: `Phase_Step[]` covers 7 octaves, A7 (index 0) down to A0
  (index 420, `Frequency_Count` 421). Each `Octave_Ranges` value is the
  offset into it of the range's highest note, 5 mm per semitone: `A2_A7` =
  0, `G2_G7` = 10, … `A1_A6` = 60, … `A0_A5` = 120 (15 ranges, one per
  natural note). Values above `A0_A5` are replaced by
  `Default_Octave_Range`. A change of range starts the pitch ramp like a
  change of distance. The pitch ramp takes the difference of two steps as
  `int32_t`, so the tool refuses to generate if the largest step is
  ≥ 2^31.
- `Phase_Step[]` is a 32-bit phase increment per sample, indexed by
  `Distance - Highest_Note_MM + Octave_Range`. Accumulate into a
  `uint32_t` phase; `phase >> Phase_Shift` (22) gives the 10-bit
  `Wave_Table` index (`Sample_Count` = 1024).
- Pitch ramp (hand-written in `dds_generator.c`): when the distance changes,
  the phase step glides linearly from its current value to the new
  `Phase_Step[]` entry over 2048 samples (46 ms, just under the 50 ms poll
  period), landing exactly on the target. The ramp is on the phase step, not
  the distance in whole mm (1 mm = 1/60 octave would step audibly), and a new
  target mid-ramp starts from wherever the ramp is. The per-sample increment
  uses a shift, not a division: the SDK's divide routines are in flash, and
  `DDS_Generator` must make no calls out of RAM (check with `objdump`).
- `Wave_Table[Waveforms][1024]` holds one cycle of each waveform as
  `int32_t` (full 32 bit scale): `Sine`, `Sine_2` (sine through `0.5*Y^2 + 0.75*Y - 0.25`,
  emulating vacuum-tube second-order distortion), `Triangle`, `Square`. All
  four are RMS-levelled to the same loudness with zero DC offset, so peaks
  differ per waveform.
- Volume: `Volume_Table[256]` is a Q31 gain (0 dB = 2^31 − 1, not 2^31, so
  the difference of two gains fits an `int32_t`), indexed by
  `Volume_Distance - Loud_MM`. 100 mm (`Loud_MM`) is 0 dB, each mm is
  −0.4 dB down to −102.4 dB at 354 mm, and 355 mm (`Mute_MM`) is 0.
  Out-of-range distances clamp. A 32 bit gain is needed: a 16 bit one can't
  give distinct 0.4–0.5 dB steps below about −72 dB.
- Gain ramp (hand-written): the same pattern as the pitch ramp, a linear
  glide over 2048 samples on each change of volume distance, landing exactly
  on the target. The gain starts at 0, so power-up is silent.
- Gain multiply: the Cortex-M0+ has only a 32×32→32 multiply and a 64 bit
  product would call `__aeabi_lmul` in flash, so `Apply_Gain()` forms
  Sample × Gain / 2^31 from three 16×16 partial products (3 `muls`,
  within 3 LSB of exact).
- The tables are emitted `static const` with `__not_in_flash("dds")` so they
  live in RAM (section `.time_critical.dds`, ~18 KB) and sample generation
  never stalls on an XIP flash cache miss. Consequently `dds_table.h`
  includes `pico.h` and only compiles inside the Pico SDK build.
- `dds_table.h` is intended to be included only by `dds_generator.c`.
  `static` makes a second include safe but would duplicate the tables in RAM.

## Firmware architecture

- `Api/vl53l4cd/` — ST's VL53L4CD Ultra Lite Driver v2.2.3
  (STSW-IMG026, BSD licence in `LICENSE.txt`; the full download is in
  `Documents/VL53L4CD_ULD_V2.2.3/`). `VL53L4CD_api.c/.h` and
  `VL53L4CD_calibration.c/.h` are untouched from the vendor drop; only
  `VL53L4CD_api.c` is built (the calibration file is kept for later). Don't
  modify them unless fixing an actual bug. `SetRangeTiming` uses float
  (soft float, at init only).
  - `platform.h`/`platform.c` — the porting layer the ULD calls into
    (`VL53L4CD_RdByte` … `WrDWord`, `WaitMs`), written for the Pico SDK.
    Register indices are 16 bit, big-endian, then big-endian data; reads use
    a repeated start. `Dev_t` is a `uint16_t` passed by value: the ULD
    normally holds just the 8-bit address (0x52) in it, so the high byte
    holds the bus (`VL53L4CD_Dev(bus)`). Both sensors are at 0x29 (7 bit),
    but on different controllers.
- `src/i2c_bus.c` — the two I2C controllers (until stage 10 this was in the
  VL53L0X porting layer). A table maps bus 0 = I2C0 on GP0/GP1 (pins 1/2),
  1 = I2C1 on GP2/GP3 (pins 4/5). Transfers use `i2c_*_timeout_us`
  (`I2C_Bus_Timeout_US`, 20 ms), not the blocking calls, and
  `I2C_Bus_Init()` first clears a stuck bus: if SDA is low it clocks SCL
  (up to 9 pulses) and sends a STOP, printing "I2Cn SDA was held low".
  This is needed because a Pico reset (`picotool load -f`/`reboot`)
  mid-transfer leaves a sensor holding SDA low. With blocking calls the
  next boot hangs silently at "Initialising Volume ...".
- `src/main.c` — application entry point. After the 2 s USB delay it
  initialises I2C0 (`I2C_Bus_Init()`, which also clears a stuck
  bus) and the display, and shows the start-up screen for at least 10 s (`SPLASH_MS`)
  while it checks the 125 MHz clock, initialises the pitch sensor and then
  the volume sensor (`init_sensor()`), starts
  the I2S output, initialises the PCM5122 and starts the encoder. Fatal
  errors are printed on USB serial and shown on display line 2 (`fatal()`).
  `init_sensor()` soft-resets the sensor (register 0x0000: 0, then 1), waits
  `BOOT_MS` and then for the firmware boot status (0x00E5 = 3), checks the
  sensor ID (0xEBAA), runs
  `VL53L4CD_SensorInit`, sets a 40 ms timing budget with a 50 ms
  inter-measurement period (`RANGING_BUDGET_MS`, `POLL_PERIOD_MS`) and
  starts ranging, so each sensor then ranges on its own at 20 Hz, menu
  included.
  It then alternates `Menu_Run()` and `play()`. `play()` sets the
  waveform and octave range, shows the waveform on line 1 and
  "Range A1 to A6" (from `Range_Name()`) on line 2, clears both sensors'
  interrupts (discarding readings taken in the menu) and
  runs a loop every 50 ms
  (20 Hz) until the encoder is pushed, then mutes. Each loop calls
  `read_range()` for the pitch and then the volume sensor: it waits (up to
  `RANGING_TIMEOUT_MS`, 70 ms) for data ready, reads the result and clears
  the interrupt; a reading is valid when `range_status` is 0. The pitch
  distance goes straight to `I2S_Output_Set_Distance()` (the DDS
  generator clamps and ramps it). Out-of-range or failed pitch measurements
  send `Lowest_Note_MM`. The volume distance goes straight to
  `I2S_Output_Set_Volume()`; out-of-range or failed readings
  send `Mute_MM`. The DDS generator clamps the volume distance and ramps
  the gain.
  There is no distance filter (removed in stage 5), so single-reading
  spikes and dropouts are not rejected. There is no display traffic while
  playing. Measurement problems are printed only when the status
  changes (`pico_enable_stdio_usb` is on, UART stdio is off).
- `src/dds_generator.c` — `DDS_Generator()`, see the DDS design above.
- `src/encoder.c` — SR1230 rotary encoder (30 detents) on GP10/GP11 and
  its push switch on GP12, sampled every 1 ms from a repeating timer (not
  the main loop, so turns aren't missed during ~2 ms display writes). A
  Gray-code transition table ignores invalid (bounce) transitions. The
  SR1230 goes through a full quadrature cycle per detent and rests at
  A = B = 1. A detent is counted on reaching that rest state if at least
  half a cycle was seen, so a missed transition can't put the count out of
  step. `Encoder_Direction` is −1 so that clockwise counts up. The push
  switch needs 20 ms stable to press, and 20 ms stable to release before
  the next press. `Encoder_Take_Detents()`, `Encoder_Take_Press()` and
  `Encoder_Flush()` read and clear the counts with interrupts disabled.
- `src/dfr0555_display.c` — C port of the Ada `DFR0555_Display`
  (`/home/david/Ada/Pi_Common/src/`), version 1.1 module only: AiP31068 LCD
  at 0x3E (display on, cursor off, whole 16-character lines, no Clear) and
  SN3193 backlight at 0x6B (OUT1 only, PWM register 0x04). The SN3193
  current is 17.5 mA (`Documents/SN3193.pdf` table 6). The LCD once
  washed out above about half PWM at 17.5 mA, so stage 7 used 10 mA; that
  was traced to the Pico's 3.3 V supply sagging, not the LED current. Failures are printed on
  USB serial only when the state changes and never stop the theremin. If
  `DFR0555_Init()` fails (it once got a NACK just after a flash), the next
  `DFR0555_Put_Line()` retries it first: the LCD powers up with the display
  off, so a later write alone would show nothing.
- `src/menu.c` — `Menu_Run()` mutes the audio, then polls the encoder every
  10 ms. Items Play, Brightness, Waveform and Range are rows in the
  `Items` table (name, value formatter, edit handler); a new item is one
  row and one field in `struct settings`. Turning moves between items (wrapping), a
  push on Play returns, and a push on any other item edits it. While
  editing, turning changes the value and a push confirms. `>` marks what
  turning changes. Brightness has 16 half-stop levels (PWM 1…255, default
  8), applied live and stopping at the ends. Range steps through the
  `Ranges` table (lowest to highest, so clockwise goes up) and also stops
  at the ends; Waveform wraps. Defaults come from `Default_Waveform` and
  `Default_Octave_Range`. The display is rewritten only
  when a line changes.
- `src/audio_i2s.pio` — 8-instruction I2S transmitter: 32-bit stereo,
  64 BCK per frame (BCK = 2.841 MHz), 2 PIO cycles per bit, integer clock
  divider `PIO_Clock_Divisor` (22) from the 125 MHz system clock. Each
  frame is two 32-bit FIFO words, left (LRCK low) then right.
- `src/i2s_output.c` — PIO0 plus two DMA channels chained ping-pong over two
  128-frame (256-word) buffers (~2.9 ms each). The DMA_IRQ_0 handler
  refills the finished buffer from `DDS_Generator()` and rearms it. The
  pitch distance, volume distance and waveform are passed from the main
  loop through volatiles (`I2S_Output_Set_Distance()` /
  `I2S_Output_Set_Volume()` / `I2S_Output_Set_Waveform()`). The volume
  distance starts at `Mute_MM`. The handler, the
  buffers and the DDS code/tables all live in RAM.
  `I2S_Output_Report_Status()` (printed only when `PCM5122_Init()` fails)
  shows the state machine/DMA state and, for each I2S pin, the transitions
  the PIO drives against those read back from the pad over 1 ms. Driven
  transitions with none read back mean something external is holding the
  pin — that is how the dead GP28 was found.
- `src/pcm5122.c` — DAC set-up over I2C1. No master clock (SCK) is wired, so
  the PCM5122 runs in 3-wire mode with its PLL referenced to BCK (datasheet
  §8.3.6.3); the I2S stream must already be running when `PCM5122_Init()`
  is called. Register sequence (page 0): standby → reset → PLL ref = BCK
  (0x0D=0x10) → ignore SCK detection/halt (0x25=0x18) → I2S 32-bit
  (0x28=0x03) → volume 0 dB (0x3D/0x3E=48, `PCM5122_Volume_0dB`; never
  written again, the volume is in the DDS, so I2C1 is otherwise used only
  by the volume sensor. Don't start at 0xFF, mute: it leaves the DAC stuck
  in power state 4, volume ramp up, and it never reaches Run) → unmute →
  leave standby, then wait for reg 118 power state 0x5 (Run).
  `PCM5122_Report_Status()` prints regs 4, 91, 94, 95 and 118 (PLL lock,
  detected FS, clock status/errors, power state) at every boot and when Run
  isn't reached. A healthy boot shows `4=0x01 91=0x30 94=0x40 95=0x10
  118=0x85`: reg 91's low nibble (SCK ratio error), reg 94's SCK missing
  and reg 95's latched SCK halt are expected with no SCK. If the DAC doesn't acknowledge at all it prints the I2C1
  idle levels and a bus scan with acknowledged/NACK/timeout counts — all
  NACKs means a working bus with nothing answering (in practice: the DAC
  board's power supply was missing).
  The PLL locks at 64 BCK/frame. If it ever won't, the next thing to try is
  disabling clock autoset (DCAS) and setting the PLL manually.

### Sensor notes

The VL53L4CD sensors are not calibrated yet (deferred from stage 10): no
offset is set (`VL53L4CD_SetOffset`, default 0) and readings are used raw.
The VL53L0X calibration (a −16 mm pitch offset and a two-point line for the
volume sensor) was for the old parts and was removed. When calibrating,
measure a flat card at 100/200/300 mm on each sensor; `VL53L4CD_calibration.c`
has ST's offset and crosstalk routines.

Raw readings on the breadboard (stage 10), flat card:

| Card | Pitch reads | Volume reads |
|---|---|---|
| 100 mm | ≈ 109 | — |
| 200 mm | ≈ 205 | — |
| 300 mm | 335–385, creeping up | 322–327, steady |

The pitch sensor's error grows sharply above 200 mm, so distances past
about 250 mm clamp to the lowest note and the pitch range sounds
compressed. The volume range sounds right. Moving the working ranges
closer to the sensors is planned for reliability.

Each boot soft-resets both sensors, because XSHUT isn't wired and a sensor
keeps its state through a Pico reset. When the new sensors were first
powered, the old VL53L0X firmware ran against them and left the pitch sensor
held in soft reset: it acknowledged, but every register (ID included) read 0.
Don't access a sensor within tBOOT (1.2 ms max) of releasing the reset:
doing so made the volume sensor report itself booted and then NACK the next
index byte (with SCL idling low), so `init_sensor()` waits `BOOT_MS` (2 ms)
first. `VL53L4CD_SensorInit` ORs every transfer status together, so a single
dropped transfer during its boot poll fails it; the boot wait in
`init_sensor()` avoids that. The ULD's status 255 means either a timeout or
a failed transfer (the platform layer returns 255 too).

With the VL53L0X, a sensor left in a bad state by wiring faults or a
mid-write reset read about 0.60× true distance, and once acknowledged its
address but NACKed every data byte. Only a power cycle of the sensor fixed
it, since XSHUT isn't wired. If readings suddenly jump, power-cycle the
sensors before recalibrating.

If the volume sensor doesn't answer, main.c prints an I2C1 idle-level check
and bus scan (`PCM5122_Scan_Bus()`); a scan with only 0x4D means the sensor
is miswired or unpowered.

`range_status` on a measurement is a sensor-reported quality code, not a
plumbing error: a non-zero status with nothing in front of the sensor is
expected (see `Documents/vl53l4cd_user_guide.pdf` for the codes).
