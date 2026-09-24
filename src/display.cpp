#include "display.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "calibration.h"
#include "config.h"
#include "input.h"
#include "orientation.h"
#include "sensors.h"
#include "settings.h"

static Adafruit_SSD1306 oled(128, OLED_H, &Wire, -1);
static bool present = false;

bool displayPresent() { return present; }

bool displayBegin() {
  present = sensorsProbe(OLED_ADDR) && oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  return present;
}

void displaySplash() {
  if (!present) return;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println(F("SatPointer"));
  oled.display();
}

// Calibration shows per-axis swing, because "not enough rotation" is the usual
// failure and it is much easier to fix while you can watch the numbers grow.
static void drawCalibrating() {
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println(F("CALIBRATING"));
  oled.println(F("rotate all ways"));
  for (int i = 0; i < 3; i++) {
    long span = calSpan(i);
    oled.printf("%c %6ld ", 'X' + i, span);
    int16_t w = constrain(span / 40, 0, 60);
    oled.drawRect(66, 22 + i * 10, 62, 7, SSD1306_WHITE);
    oled.fillRect(66, 22 + i * 10, w, 7, SSD1306_WHITE);
    oled.println();
  }
  oled.setCursor(0, 56);
  oled.print(F("send 'c' to finish"));
}

static void drawAngles() {
  Angles o;
  if (!orientationAngles(o)) {
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println(F("no fix"));
    return;
  }

  float az = trimmedAz(o);
  float dAz = azError(az);
  float dEl = cal.targetEl - o.elev;

  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.printf("AZ%6.1f ->%6.1f", az, cal.targetAz);
  oled.setCursor(0, 10);
  oled.printf("EL%6.1f ->%6.1f", o.elev, cal.targetEl);
  oled.drawFastHLine(0, 20, 128, SSD1306_WHITE);

  // The whole point of the tool: which way to turn, and how far. Show the word
  // before the number so it reads at a glance while you are moving the dish.
  if (fabs(dAz) < 0.5) {
    oled.setCursor(0, 26);
    oled.print(F("az  ON TARGET"));
  } else {
    oled.setCursor(0, 26);
    oled.printf("turn %-5s %5.1f", dAz > 0 ? "RIGHT" : "LEFT", fabs(dAz));
  }
  if (fabs(dEl) < 0.5) {
    oled.setCursor(0, 38);
    oled.print(F("el  ON TARGET"));
  } else {
    oled.setCursor(0, 38);
    oled.printf("%-10s %5.1f", dEl > 0 ? "raise" : "lower", fabs(dEl));
  }

  // Status row: which axis the dial edits, how the device is mounted, and
  // whether azimuth can be trusted at all.
  oled.setCursor(0, 54);
  oled.print(inputEditEl() ? F("az  [EL]") : F("[AZ]  el"));
  oled.setCursor(50, 54);
  oled.print(mountName(cal.mount));
  oled.setCursor(92, 54);
  oled.print(cal.magCal ? F("cal ok") : F("NO CAL"));
}

void displayDraw(bool sensorOk) {
  if (!present) return;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  if (!sensorOk) {
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println(F("I2C read error"));
    oled.println();
    oled.printf("mpu6050 %s\n", sensorsProbe(MPU_ADDR) ? "ok" : "MISSING");
    oled.printf("qmc5883 %s\n", sensorsProbe(QMC_ADDR) ? "ok" : "MISSING");
  } else if (calActive()) {
    drawCalibrating();
  } else {
    drawAngles();
  }
  oled.display();
}
