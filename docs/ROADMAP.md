# SatPointer roadmap

Hardware POC is done (see the Status section of the main README). This is the
software we want next, and the decisions already made about it.

## 1. Mount orientation

The device clamps to the beam in one of four ways. Today the pointing axis is
hardcoded to forward (+X) at `src/satpointer.cpp:34`; it becomes a runtime
setting.

Four options, spanning all three board axes, with X used both ways for
convenience:

| Name | Beam axis | Device orientation |
|---|---|---|
| `top` | +Y | beam out the left face |
| `down` | −Z | beam out the bottom, screen faces up |
| `left` | −X | beam out the back |
| `right` | +X | beam out the front (today's default) |

Axes are the **board frame** the code already defines — X = the GY-521's
printed +X arrow, Y = left, Z = up — not raw chip axes; the GY-271 is already
remapped onto that frame.

**Implementation:** add one remap step that rotates the sensor vectors into the
canonical forward/left/up frame, so `computeAngles()` runs unchanged with
forward still `(1,0,0)`. The verified fusion maths is not touched; the mount
becomes a table:

```c
struct Mount { const char *name; int8_t beam; int8_t up; };
// ±1/±2/±3 = board ±X/±Y/±Z, same encoding as ACC_AXIS
const Mount MOUNTS[] = {
  { "top",   +2, +3 },
  { "down",  -3, +1 },
  { "left",  -1, +3 },
  { "right", +1, +3 },
};
```

**Open:** the `up` column. The beam axis fixes azimuth and elevation, but roll
needs a second reference, and roll is dish skew. The values above are merely
perpendicular to the beam — they will produce a roll number, not necessarily
zero when the dish is level. Likely answer is a per-mount roll offset zeroed in
the field with the beam level, same pattern as the existing `n` bearing trim,
which also absorbs clamp slop no table can predict.

**Couples to calibration:** hard/soft-iron correction is in sensor frame, so it
survives moving the sensor relative to itself — but moving to a different face
of a metal beam changes the surrounding ferrous mass entirely. Changing mount
should prompt for recalibration, and profiles probably want storing per mount.

## 2. Calibration

The min/max hard-iron fit plus crude scale needs a real quality signal. The
symptom is already visible: `dip` swings 59°–75° with tilt when it should hold
constant.

- **Use dip variance as the live quality metric.** It is the one number that
  says "your azimuth is lying to you", and it needs no reference instrument.
- Keep the per-axis coverage bars; add a pass/fail on dip stability at the end.
- Consider an ellipsoid fit instead of min/max — noticeably better and
  affordable on this chip.
- Calibrate **with the device mounted on the beam**. A dish reflector is a
  large distorting mass.

## 3. WiFi, mDNS, NTP, OTA

- **OTA belongs in this phase**, not later. `ArduinoOTA` removes the manual
  GPIO0 flash-mode step, which has been the single biggest time sink so far.
- AP-fallback credential portal rather than a hardcoded SSID, so changing
  networks does not mean reflashing.
- mDNS → `satpointer.local`.
- **NTP is optional.** gpredict does the orbital maths on the PC. Time only
  earns its place if onboard pass prediction happens later — defer it.

Two hardware risks gate this phase:

- **Power.** WiFi TX pulls ~350 mA peaks. A bare ESP-12F fed from a USB-UART
  adapter will brown out. Needs a proper 3.3V supply with bulk capacitance
  before WiFi goes in.
- **Magnetometer interference.** Those same current spikes are a changing
  magnetic field next to the QMC5883L. Expect azimuth jitter correlated with
  transmit. Measure it early; mitigations are physical separation and twisted
  supply leads.

## 4. gpredict support — rotctld on TCP 4533

Emulate hamlib's rotctld, which gpredict drives natively. You are the rotator;
you turn the dish by hand following the display.

```
> P 145.0 28.5      set target      < RPRT 0
> p                 read position   < 90.2
                                    < -1.5
> S                 stop            < RPRT 0
> q                 quit
```

Non-blocking, one client, parsed line by line.

**Get this right up front:** the current code persists the target to flash 2 s
after it changes. gpredict sends `P` continuously while tracking a pass — that
is thousands of flash writes per pass and a dead sector. **Network-set targets
must be RAM-only; only encoder-set targets persist.**

## 5. Guidance graphics

A line-drawn beam-and-device glyph with a curved arrow for which way to rotate.
Line-drawn, not bitmap — Adafruit_GFX cannot rotate bitmaps, so the glyph is
computed from the angle.

Worth pairing with a **bullseye**: current position as a dot, target at centre,
both axes at once. Drive the dot to the middle. That reads faster than two
numbers when you are up a ladder with one hand on the dish.

Also: **roll is dish skew** — LNB polarisation angle. It may deserve promoting
from a diagnostic to a first-class target alongside azimuth and elevation.

## 6. HTTP config page

Splits the UI properly: **OLED for field guidance, browser for setup.** Keeps
the encoder doing one job instead of growing a menu tree on a 128×64 panel.

- **In-core `ESP8266WebServer`, not ESPAsyncWebServer.** Plenty for a config
  page, one less dependency; it blocks a few ms per request, which the 50 Hz
  sample loop absorbs.
- **Embed the page in PROGMEM, not LittleFS.** A few KB of HTML means one
  firmware image and no separate filesystem upload step.

Moves to the browser: WiFi credentials, mount orientation and roll offset,
azimuth trim, calibration run with live coverage and dip-variance quality,
target presets. Stays on the device: the target dial and the guidance screen.
Serial commands stay as the no-WiFi fallback.

## Cross-cutting

- **Split `satpointer.cpp` before WiFi lands, not after** — sensors /
  orientation / ui / net / settings. It is ~600 lines now and about to double.
- **Settings outgrow the EEPROM struct.** The magic number already needed one
  bump. JSON in flash is more forgiving to schema changes and maps directly
  onto the config form.

## Sequencing

1. Refactor, mount orientation, calibration quality — no WiFi, no power risk
2. Fix the supply, then WiFi + OTA (OTA early makes everything after painless)
3. rotctld
4. HTTP config page
5. Guidance graphics

Front-loads everything that does not depend on solving the power problem.
