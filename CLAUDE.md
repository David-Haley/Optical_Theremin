# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An optical theremin for a Raspberry Pi Pico W: an ST VL53L0X time-of-flight
sensor measures hand distance (mm) over I2C, and a DDS (direct digital
synthesis) tone generator turns that distance into a tone, streamed over I2S
to a PCM5122 DAC (PiFi DAC+ V2.0).

Current state (stage 3, spec in `Documents/Stage_3.md`; stage 2 in
`Stage_2.md`): the sensor is polled at 20 Hz and its filtered distance sets
the pitch; the waveform is selected by grounding one of four GPIO inputs; the
DAC volume is fixed at 0 dB. USB serial carries diagnostic and error messages only — no
distance readings. The CMake target is still `distance_measurement`, from
the distance-measurement app the project grew from.

### Pins (Pico physical pin → GPIO)

| Signal | Pin | GPIO | Peripheral |
|---|---|---|---|
| VL53L0X SDA / SCL | 1 / 2 | GP0 / GP1 | I2C0, addr 0x29 |
| PCM5122 SDA / SCL | 4 / 5 | GP2 / GP3 | I2C1, addr 0x4D |
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

Output: `build/distance_measurement.uf2` and `.elf`.

## Flash and monitor

```bash
picotool load -f -v -x build/distance_measurement.uf2
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
- `../src/dds_table.h` — `Frequency_Count`, `Phase_Step[]` and
  `Wave_Table[][]`.
- `../Documents/Frequency.csv` and `../Documents/Sample.csv` — the same data
  for inspection/plotting (`Documents/` is git-ignored except the VL53L0X PDF).

**`dds_generator.h` and `dds_table.h` are machine-generated and committed.**
Don't hand-edit them — change `Write_Headers` in `optical_theremin.adb`,
rebuild, rerun, and commit the `.adb` and regenerated headers together.

### DDS design (encoded in the generator)

- Sample rate 44.1 kHz; `DDS_Generator()` returns one stereo `Audio_Sample`
  per call.
- Pitch: A0 (27.5 Hz) up 8 octaves to 7040 Hz, 40 mm per octave
  (exponential in distance). Highest note at `Highest_Note_MM` (60 mm),
  lowest at `Lowest_Note_MM` (380 mm). Out-of-range distances clamp to the
  nearest end.
- `Phase_Step[]` is a 32-bit phase increment per sample, indexed by
  `Distance - Highest_Note_MM` (index 0 = highest note). Accumulate into a
  `uint32_t` phase; `phase >> 22` gives the 10-bit `Wave_Table` index.
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
    implementation). I2C0 on GPIO0 (SDA) / GPIO1 (SCL) — physical header pins
    1 and 2 — is hardcoded here via `VL53L0X_I2C_INSTANCE/_SDA_PIN/_SCL_PIN`.
  - `src/vl53l0x_platform.c` — generic register read/write glue that calls
    into `vl53l0x_i2c_platform.c`; only patched to replace one Windows-only
    delay call with `sleep_ms()`.
  - `src/vl53l0x_i2c_win_serial_comms.c` and `src/vl53l0x_platform_log.c` are
    the original Windows-only files, kept for reference but **not** listed in
    `CMakeLists.txt`'s sources — don't add them to the build.
- `src/main.c` — application entry point: runs the VL53L0X init sequence,
  starts the I2S output, initialises the PCM5122, then every 50 ms (20 Hz,
  `sleep_until`) takes a single ranging measurement and passes the distance
  through `filter_distance()` to `I2S_Output_Set_Distance()`. Out-of-range
  or failed measurements feed `Lowest_Note_MM` into the filter. The filter
  clamps to `Highest_Note_MM..Lowest_Note_MM`, takes a median of 3 (rejects
  single spikes/dropouts), then an EMA with alpha = 1/2^`SMOOTHING_SHIFT`
  (1 → ~100 ms lag) in fixed point with 4 fractional bits. It also polls
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
  (0x28=0x00) → volume 0 dB (0x3D/0x3E=0x30) → unmute → leave standby, then
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
`VL53L0X_PerformRefSpadManagement` → `VL53L0X_SetDeviceMode`.

`RangeStatus` on a measurement is a sensor-reported quality code, not a
plumbing error — e.g. status 4 (`PHASE_FAIL`) just means no target is in
range, which is expected with nothing in front of the sensor.
