# Optical Theremin

Firmware for a Raspberry Pi Pico W that reads distance in millimeters from an
ST VL53L0X time-of-flight sensor over I2C and reports it over USB serial.

## Hardware

- Raspberry Pi Pico W
- ST VL53L0X time-of-flight distance sensor
- Connected via I2C0 on GPIO0 (SDA) / GPIO1 (SCL) — physical header pins 1
  and 2

## Building

Requires the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
and the `arm-none-eabi` toolchain. Set `PICO_SDK_PATH` to point at your SDK
checkout, then:

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

This produces `build/distance_measurement.uf2`.

If your installed `picotool` is older than the version this SDK checkout
requires, add `-DPICOTOOL_FORCE_FETCH_FROM_GIT=1` to the `cmake` configure
step to have it built from source automatically.

## Flashing

With [picotool](https://github.com/raspberrypi/picotool) installed:

```bash
picotool load -f -v -x build/distance_measurement.uf2
```

`-f` forces the board to reboot into BOOTSEL mode for the load even if it's
currently running application code, and reboots it back into the application
afterward. Alternatively, hold BOOTSEL while plugging in the Pico and drag
the `.uf2` file onto the `RPI-RP2` USB drive that appears.

## Usage

Once flashed, connect to the board's USB serial port (e.g. `/dev/ttyACM0` on
Linux) at any baud rate to see live readings:

```
Distance: 85 mm
Distance: 82 mm
Distance: 87 mm
```

When no object is within range, the sensor reports a range status instead of
a distance (e.g. `Out of range (status 4)`).

## Project layout

- `src/main.c` — application entry point: sensor init and measurement loop.
- `Api/core/` — ST's manufacturer VL53L0X API (ranging/calibration
  algorithms), unmodified.
- `Api/platform/` — the porting layer connecting the vendor API to the Pico
  SDK's `hardware_i2c` driver.
- `Documents/` — the ST VL53L0X API datasheet.

See `CLAUDE.md` for more detailed build/architecture notes.

## License

This project's own code is licensed under the GNU General Public License
v3.0 — see [LICENSE](LICENSE). The vendor VL53L0X API files under `Api/`
retain STMicroelectronics' original BSD-3-Clause license, stated in each
file's header.
