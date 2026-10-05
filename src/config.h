// Pin assignments and tuning constants for the whole firmware.
#pragma once

#include <Arduino.h>

// ---- I2C bus (all three devices share it) ----
const uint8_t MPU_ADDR = 0x68;
const uint8_t QMC_ADDR = 0x0D;
const uint8_t OLED_ADDR = 0x3C;
const uint8_t PIN_SDA = 4;
const uint8_t PIN_SCL = 5;

// ---- Rotary encoder (EC11-style, 4 quadrature counts per detent) ----
// The button sits on GPIO0, which is also the boot strapping pin: holding it
// down through a reset enters the flash bootloader instead of the app. That is
// useful rather than harmful - it doubles as a flash button - but it does mean
// the pin must idle high, hence INPUT_PULLUP.
const uint8_t PIN_ENC_A = 13;
const uint8_t PIN_ENC_B = 14;
const uint8_t PIN_ENC_BTN = 0;
const int8_t COUNTS_PER_DETENT = 4;
const float STEP_DEG = 1.0;
const unsigned long DEBOUNCE_MS = 30;
const unsigned long LONGPRESS_MS = 800;

// ---- Timing ----
const float ALPHA = 0.2;             // low-pass factor per sample, lower = smoother
const unsigned long SAMPLE_MS = 20;  // 50 Hz
const unsigned long PRINT_MS = 100;  // 10 Hz
const unsigned long DRAW_MS = 200;   // 5 Hz, a full SSD1306 frame is not cheap
const unsigned long TARGET_SAVE_MS = 2000;

const float QMC_LSB_PER_UT = 30.0;  // +-8 G range: 3000 LSB/G

// ---- SatNOGS rotator (rotator_gcode behind esp-link's TCP serial bridge) ----
// Port 23, not 2323: esp-link resets the AVR on every 2323 connect, which
// drops the rotator's reference and disarms it.
#define ROTATOR_HOST "satnogs-rotor1"
const uint16_t ROTATOR_PORT = 23;
const float ROTATOR_EL_SIGN = -1.0;  // negative E is up on this build, as in tracker/
const float ROTATOR_WRAP_DEG = 360.0;  // travel either side of the declared position
const unsigned long ROTATOR_REPLY_MS = 5000;
