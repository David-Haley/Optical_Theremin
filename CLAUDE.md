# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An optical theremin for a Raspberry Pi Pico W: an ST VL53L0X time-of-flight
sensor measures hand distance (mm) over I2C, and a DDS (direct digital
synthesis) tone generator turns that distance into a tone, streamed over I2S
to a PCM5122 DAC (PiFi DAC+ V2.0).

Current state (stage 5, spec in `Documents/stage_5.md`; earlier stages in
`Stage_2.md`–`Stage_4.md`): two VL53L0X sensors are polled at 20 Hz. The
pitch sensor's distance sets the pitch and the volume sensor's distance sets
the PCM5122 digital volume. Readings are not filtered; instead the DDS
generator ramps the pitch and the main loop ramps the volume between
readings. The waveform is selected by grounding one of four GPIO inputs. USB
serial carries diagnostic and error messages only — no distance readings.
The CMake target is `optical_theremin` (it was `distance_measurement`, from
the distance-measurement app the project grew from).

### Pins (Pico physical pin → GPIO)

| Signal | Pin | GPIO | Peripheral |
|---|---|---|---|
| Pitch VL53L0X SDA / SCL | 1 / 2 | GP0 / GP1 | I2C0, addr 0x29, 400 kHz |
| PCM5122 SDA / SCL | 4 / 5 | GP2 / GP3 | I2C1, addr 0x4D, 100 kHz |
| Volume VL53L0X SDA / SCL | 4 / 5 | GP2 / GP3 | I2C1 (shared), addr 0x29 |
| I2S DIN | 31 | GP26 | PIO0 |
| I2S BCK | 26 | GP20 | PIO0 side-set |
| I2S LRCK | 27 | GP21 | PIO0 side-set (must be BCK + 1) |
| Select Sine / Sine_2 | 14 / 15 | GP10 / GP11 | input, pull-up, active low |
| Select Triangle / Square | 16 / 17 | GP12 / GP13 | input, pull-up, active low |

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

- `../src/dds_generator.h` — public interface: `Sample_Rate`,
  `Highest_Note_MM`, `Lowest_Note_MM`, the `Waveforms` enum, the
  `Audio_Sample` struct and the `DDS_Generator()` prototype.
- `../src/dds_table.h` — `Frequency_Count`, `Phase_Step[]`,
  `Sample_Count`, `Phase_Shift` and `Wave_Table[][]`.
- `../Documents/Frequency.csv` and `../Documents/Sample.csv` — the same data
  for inspection/plotting (`Documents/` is git-ignored except the VL53L0X PDF).

**`dds_generator.h` and `dds_table.h` are machine-generated and committed.**
Don't hand-edit them — change `Write_Headers` in `optical_theremin.adb`,
rebuild, rerun, and commit the `.adb` and regenerated headers together.

### DDS design (encoded in the generator)

- Sample rate 44.1 kHz; `DDS_Generator()` returns one stereo `Audio_Sample`
  per call.
- Pitch: A1 (55 Hz) up 5 octaves to 1760 Hz, 60 mm per octave
  (exponential in distance). Highest note at `Highest_Note_MM` (60 mm),
  lowest at `Lowest_Note_MM` (360 mm). Out-of-range distances clamp to the
  nearest end.
- `Phase_Step[]` is a 32-bit phase increment per sample, indexed by
  `Distance - Highest_Note_MM` (index 0 = highest note). Accumulate into a
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
  `int16_t`: `Sine`, `Sine_2` (sine through `0.5*Y^2 + 0.75*Y - 0.25`,
  emulating vacuum-tube second-order distortion), `Triangle`, `Square`. All
  four are RMS-levelled to the same loudness (~18317) with zero DC offset,
  so peaks differ per waveform.
- The tables are emitted `static const` with `__not_in_flash("dds")` so they
  live in RAM (section `.time_critical.dds`, ~9.5 KB) and sample generation
  never stalls on an XIP flash cache miss. Consequently `dds_table.h`
  includes `pico.h` and only compiles inside the Pico SDK build.
- `dds_table.h` is intended to be included only by `dds_generator.c`.
  `static` makes a second include safe but would duplicate the tables in RAM.

## Firmware architecture

- `Api/core/` — ST's manufacturer VL53L0X API (ranging/calibration
  algorithms). Platform-agnostic C, untouched from the vendor drop. Don't
  modify unless fixing an actual algorithm bug.
- `Api/platform/` — the porting layer the vendor API calls into. Only two
  files are part of the Pico build:
  - `src/vl53l0x_i2c_platform.c` — the actual I2C transport, rewritten
    against the Pico SDK's `hardware_i2c` (originally a Windows DLL-backed
    implementation). Every function takes a leading `bus` argument that
    indexes a table: 0 = I2C0 on GP0/GP1 (pins 1/2), 1 = I2C1 on GP2/GP3
    (pins 4/5). Both sensors are at 0x29, but on different controllers.
    Transfers use `i2c_*_timeout_us` (20 ms), not the blocking calls, and
    `VL53L0X_comms_initialise` first clears a stuck bus: if SDA is low it
    clocks SCL (up to 9 pulses) and sends a STOP, printing "I2Cn SDA was held
    low". This is needed because a Pico reset (`picotool load -f`/`reboot`)
    mid-transfer leaves the sensor holding SDA low. With blocking calls the
    next boot hangs silently at "Initialising Volume VL53L0X...".
  - `src/vl53l0x_platform.c` — generic register read/write glue that calls
    into `vl53l0x_i2c_platform.c`, passing the device's bus from the
    `I2cBus` field added to `VL53L0X_Dev_t` (`inc/vl53l0x_platform.h`). The
    only other patch replaces one Windows-only delay call with `sleep_ms()`.
  - `src/vl53l0x_i2c_win_serial_comms.c` and `src/vl53l0x_platform_log.c` are
    the original Windows-only files, kept for reference but **not** listed in
    `CMakeLists.txt`'s sources — don't add them to the build.
- `src/main.c` — application entry point: runs the VL53L0X init sequence
  (`init_sensor()`) for the pitch sensor and then the volume sensor, starts
  the I2S output, initialises the PCM5122, then runs a loop every 50 ms
  (20 Hz). Each loop starts a single ranging measurement on
  both sensors and then collects both (`start_ranging()`/`finish_ranging()`,
  the two halves of `VL53L0X_PerformSingleRangingMeasurement`). The sensors
  range at the same time because two back-to-back ~33 ms measurements would
  overrun the 50 ms period. The pitch distance goes straight to `I2S_Output_Set_Distance()` (the DDS
  generator clamps and ramps it). Out-of-range or failed pitch measurements
  send `Lowest_Note_MM`. Valid volume readings are first corrected by
  `correct_volume_range()` (see the calibration notes below), then clamped
  to 100..307 mm. Out-of-range or failed readings give 307. The PCM5122
  volume code is `distance − 52`: 100 mm → 48 (0 dB, the gain is capped
  there to avoid clipping), 306 → 254 (−103 dB), 307 → 255 (mute). There is
  no distance filter (removed in stage 5), so single-reading spikes and
  dropouts are not rejected. Instead `struct volume_ramp` moves the DAC to
  each new code in `VOLUME_STEPS` (25) linear steps, one every
  `VOLUME_STEP_MS` (2 ms), starting from the code last written.
  `PCM5122_Set_Volume()` is called only when the code changes. The steps are
  taken by `volume_ramp_service()` from the main thread — inside
  `finish_ranging()`'s data-ready polling loop and while waiting for the next
  poll — not from a timer IRQ, because the PCM5122 shares I2C1 with the
  volume sensor. It also polls
  `Waveform_Select_Read()` each loop and prints the waveform on change. Measurement problems are printed only when the status
  changes (`pico_enable_stdio_usb` is on, UART stdio is off).
- `src/dds_generator.c` — `DDS_Generator()`, see the DDS design above.
- `src/waveform_select.c` — waveform select inputs GP10–GP13 (pins 14–17),
  pull-ups, active low. The first low pin in the order Sine, Sine_2,
  Triangle, Square wins; none low → Sine. Debounced: a change needs two
  consecutive identical reads, so `Waveform_Select_Read()` must be called
  ≥20 ms apart (the 50 ms main loop does this).
- `src/audio_i2s.pio` — 8-instruction I2S transmitter: 16-bit stereo,
  32 BCK per frame (BCK = 1.4112 MHz), 2 PIO cycles per bit (fractional
  clock divider from the 125 MHz system clock). Each 32-bit FIFO word is one
  frame: bits 31..16 right, 15..0 left.
- `src/i2s_output.c` — PIO0 plus two DMA channels chained ping-pong over two
  128-frame buffers (~2.9 ms each). The DMA_IRQ_0 handler refills the
  finished buffer from `DDS_Generator()` and rearms it. The distance and
  waveform are passed from the main loop through volatiles
  (`I2S_Output_Set_Distance()` / `I2S_Output_Set_Waveform()`). The handler, the
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
  (0x0D=0x10) → ignore SCK detection/halt (0x25=0x18) → I2S 16-bit
  (0x28=0x00) → volume −103 dB (0x3D/0x3E=0xFE, until the main loop sets it
  from the volume sensor; starting at 0xFF, mute, leaves the DAC stuck in
  power state 4, volume ramp up, and it never reaches Run) → unmute → leave standby, then
  wait for reg 118 power state 0x5 (Run). If the PLL doesn't lock it prints
  regs 4, 91, 94, 95 and 118 (PLL lock, detected FS, clock status/errors,
  power state). If the DAC doesn't acknowledge at all it prints the I2C1
  idle levels and a bus scan with acknowledged/NACK/timeout counts — all
  NACKs means a working bus with nothing answering (in practice: the DAC
  board's power supply was missing).
  If the PLL won't lock, the next things to try are 64 BCK/frame (32-bit
  slots) or disabling clock autoset (DCAS) and setting the PLL manually.

### Known API gotcha

`VL53L0X_WaitDeviceBooted()` (declared in `Api/core/inc/vl53l0x_api.h`) is an
unimplemented stub in this API version — it always returns
`VL53L0X_ERROR_NOT_IMPLEMENTED` regardless of hardware state. Do not call it;
it's not part of the real VL53L0X init sequence. The correct sequence (as
used in `main.c`) is: `VL53L0X_comms_initialise` → `VL53L0X_DataInit` →
`VL53L0X_StaticInit` → `VL53L0X_PerformRefCalibration` →
`VL53L0X_PerformRefSpadManagement` → offset correction → `VL53L0X_SetDeviceMode`.

The pitch sensor's factory NVM part-to-part offset (125.5 mm) is wrong and made
every reading far too long, so `main.c` replaces it with `PITCH_RANGE_OFFSET_MM`
(−16 mm, set so a flat card at 300 mm reads 300) via
`VL53L0X_SetOffsetCalibrationDataMicroMeter`. The device adds the offset to
each range, so positive = longer readings. Readings also appeared to be
scaled short (~0.84–0.89) but the 100/200 mm test points weren't held
reliably, so no gain correction is applied. `VL53L0X_SetLinearityCorrectiveGain`
can only scale down (max 1000/1000), so any future gain correction must be
done in firmware.

The volume sensor keeps its factory offset (`VOLUME_RANGE_OFFSET_MM` =
`KEEP_FACTORY_OFFSET`, 46.5 mm on this part). Its error isn't a plain offset,
so `correct_volume_range()` maps readings through a straight line between
two measured points: a flat card at 100 mm read 126.2 mm, and at 300 mm read
312.8 mm (`VOLUME_CAL_*`, in tenths of a mm). At 150 mm it read 165.4 mm, about
8 mm true (4 dB) off the line; a third point would fix that if it's
audible. The calibration was confirmed at 300 mm after a sensor power cycle
and after a Pico reboot (both within 0.5 mm), and readings vary less than
2 mm raw from boot to boot.

An earlier calibration of the volume sensor was wrong because the sensor was
in a bad state after wiring faults and a mid-write reset. It read about 0.60×
true distance, and at one point it acknowledged its address but NACKed every
data byte. Only a power cycle of the sensor fixes this, since XSHUT isn't
wired. If readings suddenly jump, power-cycle the sensor before recalibrating.

`init_sensor()` prints each sensor's offset at boot. The offset lives in a
sensor register that survives a Pico reboot, so after a reflash without a
power cycle the pitch sensor shows the −16 mm override, not its factory
value. A pitch reading of about 125500 µm confirms the sensors really lost
power. If the volume sensor doesn't answer, main.c prints
an I2C1 idle-level check and bus scan (`PCM5122_Scan_Bus()`); a scan with
only 0x4D means the sensor is miswired or unpowered.

`RangeStatus` on a measurement is a sensor-reported quality code, not a
plumbing error — e.g. status 4 (`PHASE_FAIL`) just means no target is in
range, which is expected with nothing in front of the sensor.
