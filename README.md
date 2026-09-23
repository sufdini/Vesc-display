# VESC Display

A small dashboard for VESC-based electric vehicles (e-skateboards, e-bikes,
scooters, onewheels). An ESP32 talks to the VESC over UART, reads the live
telemetry and shows speed, battery, power, temperatures, trip data and faults
on a colour TFT.

Built for the **LilyGO TTGO T-Display** (ESP32 + 1.14" 135x240 ST7789), but
the protocol code is board-agnostic and the TFT setup is a few build flags.

## Screens

Press the right button to go to the next page, the left button to go back.

| Page   | Shows                                                                 |
| ------ | --------------------------------------------------------------------- |
| Main   | Big speed readout, max speed, power bar (drive/regen), battery %, power, battery current, motor and ESC temperature |
| Power  | Motor current, battery current, power, duty cycle (with bar), ERPM, pack voltage |
| Trip   | Distance, Ah used, Wh used, Wh/km (or Wh/mi), regenerated Ah, max speed |
| System | ESC and motor temperature, VESC controller id, packet / CRC counters, uptime, last fault since boot |

The status bar at the top always shows the link state (green dot = live data,
red = link lost), pack and per-cell voltage or the active **fault code**, and
a battery gauge.

## Hardware

| T-Display pin | VESC          |
| ------------- | ------------- |
| GPIO 26 (RX)  | TX            |
| GPIO 27 (TX)  | RX            |
| GND           | GND           |
| 5V            | 5V (VESC COMM port) |

Any free ESP32 GPIOs work for the UART; change `VESC_RX_PIN` / `VESC_TX_PIN`
in `include/config.h`. The VESC's COMM port is 3.3 V logic, so no level
shifting is needed.

In **VESC Tool** go to *App Settings -> General* and set *App to use* to
`UART` (or `PPM and UART` if you also have a remote on PPM), and set the UART
baud rate to 115200. Write the app configuration.

## Configuration

Edit `include/config.h`:

| Setting               | Meaning                                                     |
| --------------------- | ----------------------------------------------------------- |
| `MOTOR_POLES`         | Magnet poles of the motor (not pole pairs). Usually 14.     |
| `WHEEL_DIAMETER_MM`   | Driven wheel outer diameter.                                |
| `GEAR_RATIO`          | Wheel turns per motor turn. `1.0` for hub motors, `15.0/36.0` for a 15T:36T belt drive. |
| `BATTERY_CELLS`       | Series cell count (10S = 10).                               |
| `USE_IMPERIAL_UNITS`  | `0` for km/h and km, `1` for mph and miles.                 |
| `VESC_UART_BAUD`      | Must match the VESC UART app setting.                       |
| `VESC_TIMEOUT_MS`     | Time without data before the display reports a lost link.   |

Battery percentage is derived from pack voltage with a Li-ion discharge
curve, so it reads a little low under heavy load and recovers at rest.

## Building and flashing

Requires [PlatformIO](https://platformio.org/) (`pip install platformio`).

```sh
pio run -e tdisplay              # build
pio run -e tdisplay -t upload    # flash over USB
pio device monitor               # serial log at 115200 baud
```

The serial log prints one telemetry line per second, which is handy for
checking the UART link before the screen is wired up.

## Tests

The protocol library has host-side unit tests (framing, CRC, `COMM_GET_VALUES`
decoding, speed / distance / battery maths):

```sh
pio test -e native
```

## Bench testing without a VESC

`tools/vesc_sim.py` pretends to be a VESC on a USB-serial adapter and plays a
synthetic ride (accelerate, cruise, brake with regen, stop) with a slowly
draining battery:

```sh
pip install pyserial
python3 tools/vesc_sim.py /dev/ttyUSB1            # metric, no fault
python3 tools/vesc_sim.py /dev/ttyUSB1 --fault 5  # report OVER_TEMP_FET
```

Cross the adapter's TX/RX with the display's `VESC_RX_PIN` / `VESC_TX_PIN`.

## Project layout

```
include/config.h          vehicle, wiring and unit settings
lib/VescUart/             VESC UART protocol (host-testable, no Arduino deps)
  VescPacket.*            frame encode/decode, CRC-16
  VescValues.*            COMM_GET_VALUES decoder, fault names
  VescMath.*              ERPM -> speed, tachometer -> distance, battery %
  VescUart.*              Arduino Stream client that polls the VESC
src/Dashboard.*           TFT_eSPI dashboard renderer (sprite based, no flicker)
src/main.cpp              firmware entry point, buttons, polling loop
test/test_protocol/       Unity tests for the protocol library
tools/vesc_sim.py         fake VESC for bench testing
```

## Adapting to another board or screen

* Different ESP32 board with the same ST7789 panel: change the `TFT_*` pin
  defines in `platformio.ini`.
* Different panel supported by TFT_eSPI: swap `-DST7789_DRIVER` for the right
  driver and adjust `TFT_WIDTH` / `TFT_HEIGHT`. The layout assumes a 240x135
  landscape canvas; other sizes will need adjustments in `src/Dashboard.cpp`.
* Something other than TFT_eSPI (e.g. an OLED): reimplement `Dashboard`; the
  `DashboardState` struct already holds every number in display units.

## Protocol notes

The VESC UART protocol frames every message as
`[2][len][payload][crc16 hi][crc16 lo][3]` (a `3` start byte and 16-bit
length for payloads over 255 bytes). The CRC is CRC-16/XMODEM over the
payload. The display sends `COMM_GET_VALUES` (id 4) ten times a second and
decodes the big-endian fixed-point reply as laid out in `commands.c` of the
VESC firmware. Fields added by newer firmware (per-MOSFET temperatures, Vd/Vq,
status) are decoded when present, so firmware 3.x through 6.x all work.
