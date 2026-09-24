# SatPointer

Shows where the board is pointing, in the terms used to aim a satellite dish,
and tells you which way to turn to reach a target you dial in.

- **azimuth**: true bearing, 0° = north, 90° = east, measured clockwise
- **elevation**: angle above the horizon, from −90° to +90°
- **roll**: rotation around the pointing direction

The pointing direction is the **+X arrow printed on the GY-521**.

![board](images/board.jpg)

Ported from the Arduino Nano sketch in `docs/original-nano/` to an
ESP-12F with an OLED and a rotary encoder. The sensor maths is unchanged.

## Hardware

| Part | Chip | I2C address |
|---|---|---|
| ESP-12F on a USB-UART adapter | ESP8266EX, 4MB flash | |
| GY-521 | MPU6050 (accelerometer) | 0x68 |
| GY-271 | QMC5883L (magnetometer) | 0x0D |
| 0.96" OLED | SSD1306 128×64 | 0x3C |
| EC11 rotary encoder + button | | |

All three I2C devices share one bus:

| Signal | ESP-12F |
|---|---|
| SDA | GPIO4 |
| SCL | GPIO5 |
| VCC | **3.3V** |
| GND | GND |

| Encoder | ESP-12F |
|---|---|
| A | GPIO13 |
| B | GPIO14 |
| button | GPIO0 |
| common (middle pin of the 3-pin side) | GND |

The quadrature section and the push switch have **separate grounds**. With the
common pin floating the button still works perfectly while the dial does
nothing at all, because the internal pullups hold A and B high and no edge ever
reaches the interrupt. Send `e` to check: if A and B do not change as you turn,
that is the cause. On a KY-040 breakout, its **+** pin must also be powered,
since its pullups are on the module.

**The ESP8266 is not 5V tolerant.** The original Nano wiring ran both sensor
modules from 5V; on this board they must be on 3.3V.

**GPIO0 is the boot strapping pin.** Holding the encoder button down through a
reset enters the flash bootloader instead of the app — which is convenient, it
doubles as a proper flash button, but it means the button must idle high.

Boot pins on the bare module: EN (CH_PD) → 3.3V, GPIO15 → GND, GPIO2 floating.
Use a 3.3V adapter that can source ~300mA peak; cheap breakouts brown out
mid-flash.

**Mounting:** both sensor modules lie flat, with the GY-271's **Y** arrow
pointing the same way as the GY-521's **X** arrow. If you mount them
differently, change `ACC_AXIS` / `MAG_AXIS` at the top of `src/satpointer.cpp`.

Some GY-271 boards carry an HMC5883L (0x1E) or a QMC5883P (0x2C) instead, which
this code does not support. `pio run -e i2cprobe -t upload` identifies which one
is fitted.

## Build and flash

```sh
pio run -t upload                 # the application
pio run -e i2cprobe -t upload     # scan the bus, identify each chip
pio run -e oled -t upload         # SSD1306 only
pio run -e blink -t upload        # LED + chip info only
python3 tools/monitor.py          # serial capture (clears DTR; see below)
```

`upload_port` is pinned in `platformio.ini`. For a 0.91" panel change
`-D OLED_H=64` to `32`.

If an upload fails with `Timed out waiting for packet header`, hold the encoder
button (or ground GPIO0) while the board resets, upload, then release it.

## Use

The display shows current and target angles, and what to do about the
difference:

```
AZ  90.2 -> 145.0
EL  -1.5 ->  28.5
--------------------
turn RIGHT     54.8
raise          30.0

[AZ]  el     cal ok
```

- **turn the encoder** to move the target
- **press** to switch between editing azimuth and elevation — the bracketed
  label shows which is live
- **hold** for 0.8s to clear both targets

The target is saved to EEPROM two seconds after you stop turning, so a sweep of
the dial costs one flash write rather than one per detent.

Serial at **115200** prints 10 lines per second, as `label:value` pairs so the
Serial Plotter can graph them:

```
elev:-1.5 az:90.2 daz:54.8 del:30.0 roll:0.8 dip:65.3 B:52.1
```

| Field | Meaning |
|---|---|
| `elev` | elevation of the +X arrow |
| `az` | true azimuth of the +X arrow |
| `daz` | degrees to turn to reach the target; positive = clockwise |
| `del` | degrees to raise to reach the target |
| `roll` | rotation around +X; positive when the right side is down |
| `dip` | magnetic dip angle. It should stay the same however you turn the board. If it changes, the calibration or axis mapping is off. |
| `B` | field strength in µT. Earth's is 25–65; much more means something magnetic is nearby. |

## Calibration (do once)

The magnetometer needs calibrating, because the module headers and anything
steel nearby bend the magnetic field. Do it with everything mounted exactly as
it will be used. Until it is done the display shows **NO CAL** and azimuth
should not be trusted.

1. **Magnetometer:** send `c`, slowly rotate the board through *every*
   orientation for 30–60 seconds (including upside down), then send `c` again.
   The display shows the swing collected on each axis while this runs.
2. **North:** point the +X arrow at a known true bearing and send it. `n90`
   means it points east, `n` means north. This also absorbs magnetic
   declination.

Both are saved to EEPROM and survive a restart.

| Command | Action |
|---|---|
| `c` | start / finish magnetometer calibration |
| `nDEG` | the +X arrow is currently pointing at true bearing DEG |
| `r` | switch raw sensor output on or off |
| `e` | print encoder pin states, for the wiring check above |
| `p` | print stored settings |
| `z` | erase settings |
| `h` | help |

## How it works

At rest, the accelerometer tells which way is down (**D**), and the
magnetometer gives the magnetic field (**M**). From those two vectors:

- **East** = D × M
- **North** = East × D

Taking the +X arrow's components along north, east and down gives azimuth
`atan2(E, N)` and elevation `asin(−D)`. This works however the board is tilted.
Both sensor vectors are smoothed with a simple low-pass filter.

## Status

Verified on hardware 2026-09-22: both sensors read (field 46 µT, well inside
Earth's range), EEPROM load/store works, the encoder counts and its target
persists, and the button toggles the edited axis cleanly.

Not yet verified:

- **Encoder direction.** Counting works, but which way clockwise moves the
  target was never confirmed. If it is inverted, swap `PIN_ENC_A` and
  `PIN_ENC_B`.
- **The OLED rendering.** It enumerates at 0x3C and `display.begin()` succeeds,
  but nobody has confirmed the layout looks right on the panel.
- **Calibration.** Never run. `dip` currently swings 59°–75° as the board
  tilts, which is exactly what an uncalibrated magnetometer looks like — it
  should hold constant. Azimuth is not trustworthy until `c` is done.

## Limitations

- Azimuth isn't meaningful when +X points nearly straight up or down.
- Readings are only correct while the board is still or moving slowly. Shaking
  adds acceleration on top of gravity, which throws off the accelerometer. A
  gyro filter would fix that; the MPU6050 has a gyro, but this code doesn't use
  it yet.
- Recalibrate if you add or move anything metal or magnetic near the GY-271.

## Serial capture quirk

**Set `dtr = False` before reading, or the port goes silent.** Opening the port
with pyserial's defaults produced zero bytes at 115200 while the ROM bootloader
was visible at 74880 — i.e. the chip sat in flash mode instead of running the
app. Clearing DTR fixed it every time. `tools/monitor.py` does this; use it
rather than a bare `pio device monitor`, which also never exits on its own and
macOS has no `timeout` command to stop it.

Pulsing RTS does **not** reset this board — uptime kept climbing straight
through the attempts — so the boot banner can only be caught right after an
upload, or by resetting the board by hand. What is actually wired to DTR/RTS on
this adapter has not been established; the above is the observed behaviour, not
a schematic.

Boot output starts with garbage: the ROM bootloader talks at 74880 baud before
the sketch's 115200 banner. That's expected.
