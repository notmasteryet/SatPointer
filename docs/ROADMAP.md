# SatPointer roadmap

Hardware POC is done (see the Status section of the main README). This is the
software we want next, and the decisions already made about it.

## 1. Mount orientation — DONE 2026-09-23 (built, not yet run on hardware)

The device clamps to the beam in one of four ways. Was hardcoded to forward
(+X); now a runtime setting cycled with `m`, stored in `Settings.mount`.

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

**As built:** `toCanonical()` in `src/orientation.cpp` rotates the sensor
vectors into a frame where the beam is +X, so `orientationAngles()` runs
unchanged with the beam still `(1,0,0)`. The fusion maths was not touched.
Verified on the host: every mount maps its beam axis onto canonical +X with a
right-handed frame, and `right` is the exact identity, so the orientation the
POC ran in behaves bit-for-bit as before.

Changing mount calls `orientationReset()` — the low-pass filter holds vectors
in the old frame and blending across the change would drift through nonsense.

**Still open:** the `up` column. The beam axis fixes azimuth and elevation, but roll
needs a second reference, and roll is dish skew. The values above are merely
perpendicular to the beam — they will produce a roll number, not necessarily
zero when the dish is level. Likely answer is a per-mount roll offset zeroed in
the field with the beam level, same pattern as the existing `n` bearing trim,
which also absorbs clamp slop no table can predict.

**Couples to calibration:** hard/soft-iron correction is in sensor frame, so it
survives moving the sensor relative to itself — but moving to a different face
of a metal beam changes the surrounding ferrous mass entirely. Changing mount prints a recalibration warning; storing a
profile per mount is still to do.

## 2. Calibration

The min/max hard-iron fit plus crude scale needs a real quality signal. The
symptom is already visible: `dip` swings 59°–75° with tilt when it should hold
constant.

- **Use |B| variance as the primary quality metric, not dip.** Measured
  2026-09-23: waving the device through 135° of elevation and a full turn of
  roll gave dip a 20° spread but |B| an 11% spread. Dip is computed from the
  magnetic vector *and* the accelerometer's idea of down, so handling
  acceleration corrupts it — it conflates magnetometer error with motion. |B|
  is just the corrected field magnitude and touches no other sensor, so it
  isolates the hard/soft-iron fit. Earth's field is locally constant, so any
  variation is calibration error or interference.
- Keep dip as a secondary check, evaluated only while still: it is the thing
  that catches an accelerometer/magnetometer axis-mapping mistake, which |B|
  alone cannot see.
- Keep the per-axis coverage bars; add a pass/fail on |B| stability at the end.
- **An ellipsoid fit is probably now required, not optional.** Two graded
  attempts on 2026-09-23 scored sd 4.56% and 4.4% against a 3% threshold.
  Landing twice in the same place is a plateau rather than scatter, which
  points at the model rather than the rotation: min/max fits a diagonal
  correction only, so it cannot represent cross-axis soft iron. Simulated
  cross-axis distortion scores 5.4% — the same order as what the hardware is
  producing. A third careful attempt would confirm it before spending the code;
  if it lands near 4.5% again, stop rotating and fit an ellipsoid.
- **Calibrate in the exact final configuration, cables included.** The first
  real calibration was done with the UART unplugged (a tether fights the
  rotation) and then measured with it plugged in; hard-iron offsets are
  specific to the surrounding metal and do not transfer. A dish reflector is a
  much larger distorting mass again.

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

- ~~Split `satpointer.cpp` before WiFi lands~~ — done 2026-09-23. Now
  config / sensors / orientation / calibration / display / input / commands /
  settings, with `main.cpp` holding only the loop scheduling.
- **Settings outgrow the EEPROM struct.** The magic number already needed one
  bump. JSON in flash is more forgiving to schema changes and maps directly
  onto the config form.

## Sequencing

1. ~~Refactor~~, ~~mount orientation~~, ~~calibration grading~~ — done
2. Fix the supply, then WiFi + OTA (OTA early makes everything after painless)
3. rotctld
4. HTTP config page
5. Guidance graphics
6. Ellipsoid calibration fit — **deprioritised 2026-09-23**

The ellipsoid fit was the natural continuation of phase 1, but it is a
refinement of something that already works well enough to use: calibration
scores ~4.5% and the device says so rather than pretending otherwise, so
azimuth is usable with a known caveat instead of silently wrong. Features come
first. Pick it up when azimuth accuracy is the thing actually blocking a real
aiming job.
