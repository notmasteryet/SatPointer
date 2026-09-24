# elev_azimuth

This sketch shows where a breadboard is pointing, in the same terms used to aim a satellite dish:

- **azimuth**: true bearing, 0° = north, 90° = east, measured clockwise
- **elevation**: angle above the horizon, from −90° to +90°
- **roll**: rotation around the pointing direction

The pointing direction is the **+X arrow printed on the GY-521**.

![board](images/board.jpg)

In the photo, top is true north, so the board points east (az ≈ 90°, elev ≈ 0°).

## Hardware

| Part | Chip | I2C address |
|---|---|---|
| Arduino Nano (CH340 clone) | ATmega328P | |
| GY-521 | MPU6050 (accelerometer) | 0x68 |
| GY-271 | QMC5883L (magnetometer) | 0x0D |

Wire both modules the same way:

| Module | Nano |
|---|---|
| VCC | 5V |
| GND | GND |
| SDA | A4 |
| SCL | A5 |

**Mounting:** both modules lie flat. The GY-271's **Y** arrow points the same way as the GY-521's **X** arrow, as in the photo. If you mount them differently, change `ACC_AXIS` / `MAG_AXIS` at the top of the sketch.

Some GY-271 boards use an HMC5883L (address 0x1E) or a QMC5883P (address 0x2C) instead, and this sketch doesn't support those. To find out which one you have, run `extras/i2c_probe`: it scans the I2C bus and reads each chip's ID.

## Upload

No libraries needed. In Arduino IDE, select:

- Board: **Arduino Nano**
- Processor: **ATmega328P (Old Bootloader)**

Or from the command line:

```sh
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old elev_azimuth
arduino-cli upload -p /dev/cu.usbserial-1460 --fqbn arduino:avr:nano:cpu=atmega328old elev_azimuth
```

## Use

Open the Serial Monitor at **115200** baud. It prints 10 lines per second:

```
elev:-1.5 az:90.2 roll:0.8 dip:65.3 B:52.1
```

| Field | Meaning |
|---|---|
| `elev` | elevation of the +X arrow |
| `az` | true azimuth of the +X arrow |
| `roll` | rotation around +X; positive when the right side is down |
| `dip` | magnetic dip angle. It should stay the same however you turn the board. If it changes, the calibration or axis mapping is off. |
| `B` | magnetic field strength in µT. Earth's field is 25–65 µT; much more means something magnetic is nearby. |

The lines are `label:value` pairs, so Serial Plotter can graph them too.

## Calibration (do once)

The magnetometer needs calibrating, because the Nano, the header pins and the breadboard's steel clips all bend the magnetic field. Do it with everything mounted exactly as it will be used.

1. **Magnetometer:** send `c`, slowly rotate the board through *every* orientation for about 30–60 seconds (including upside down), then send `c` again.
2. **North:** point the +X arrow at a known true bearing and send it. For example, `n90` means it points east and `n` means north. This also takes care of magnetic declination.

Both are saved to EEPROM and survive a restart.

| Command | Action |
|---|---|
| `c` | start / finish magnetometer calibration |
| `nDEG` | the +X arrow is currently pointing at true bearing DEG |
| `r` | switch raw sensor output on or off |
| `p` | print stored settings |
| `z` | erase settings |
| `h` | help |

## How it works

At rest, the accelerometer tells which way is down (**D**), and the magnetometer gives the magnetic field (**M**). From those two vectors:

- **East** = D × M
- **North** = East × D

Taking the +X arrow's components along north, east and down gives azimuth `atan2(E, N)` and elevation `asin(−D)`. This works however the board is tilted. Both sensor vectors are smoothed with a simple low-pass filter.

## Limitations

- Azimuth isn't meaningful when +X points nearly straight up or down.
- Readings are only correct while the board is still or moving slowly. Shaking adds acceleration on top of gravity, which throws off the accelerometer. A gyro filter would fix that; the MPU6050 has a gyro, but this sketch doesn't use it yet.
- Recalibrate if you add or move anything metal or magnetic near the GY-271.
