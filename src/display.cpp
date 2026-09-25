#include "display.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "calibration.h"
#include "config.h"
#include "guidance.h"
#include "orientation.h"
#include "rotctl.h"
#include "net.h"
#include "sensors.h"
#include "settings.h"
#include "ui.h"

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
  oled.printf("n=%-4u press=save", calSamples());
}

// Shown for a few seconds after calibration ends. Without this a calibration
// run from the menu just returns to the angles screen, so a rejected one looks
// exactly like a successful one.
static void drawCalResult() {
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println(F("CALIBRATION"));
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  oled.setTextSize(2);
  oled.setCursor(0, 18);
  switch (calResult()) {
    case CAL_RESULT_OK: oled.print(F("OK")); break;
    case CAL_RESULT_POOR: oled.print(F("POOR")); break;
    default: oled.print(F("FAILED")); break;
  }

  oled.setTextSize(1);
  oled.setCursor(0, 40);
  switch (calResult()) {
    case CAL_RESULT_OK:
      oled.printf("sd %.2f%%  |B| %.0fuT", calResultSd(), cal.calField);
      break;
    case CAL_RESULT_POOR:
      oled.printf("sd %.2f%% - want <%.0f%%", calResultSd(), CAL_MAX_SPREAD_PCT);
      oled.setCursor(0, 50);
      oled.print(F("rotate more, go slow"));
      break;
    case CAL_RESULT_NO_ROTATION:
      oled.print(F("an axis barely moved"));
      oled.setCursor(0, 50);
      oled.print(F("turn it every way"));
      break;
    default:
      oled.print(F("too short to grade"));
      oled.setCursor(0, 50);
      oled.print(F("rotate for longer"));
      break;
  }
}

static void drawWifi() {
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println(F("WIFI"));
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  oled.setTextSize(2);
  oled.setCursor(0, 16);
  oled.print(netStateName());

  oled.setTextSize(1);
  oled.setCursor(0, 36);
  oled.print(netAddress());
  oled.setCursor(0, 46);
  if (netState() == NET_PORTAL) {
    oled.print(F("join it to set up"));
  } else if (netState() == NET_ONLINE) {
    oled.print(F("satpointer.local"));
  }
  oled.setCursor(0, 56);
  oled.print(cal.wifiOn ? F("press=off hold=back") : F("press=on  hold=back"));
}

// The loop stops during an update, so without this the device looks hung for
// the twenty seconds an OTA takes.
static void drawOta() {
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println(F("OTA UPDATE"));
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);
  oled.setTextSize(2);
  oled.setCursor(0, 20);
  oled.printf("%u%%", netOtaPercent());
  oled.drawRect(0, 44, 128, 10, SSD1306_WHITE);
  oled.fillRect(0, 44, netOtaPercent() * 128 / 100, 10, SSD1306_WHITE);
}

static void drawMenu() {
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println(F("MENU"));
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);
  for (uint8_t i = 0; i < uiMenuCount(); i++) {
    oled.setCursor(0, 16 + i * 10);
    oled.print(i == uiMenuIndex() ? '>' : ' ');
    oled.print(' ');
    oled.print(uiMenuLabel(i));
    // Show the value beside the item it belongs to, so the menu answers
    // "what is it set to now" without another press.
    const char *v = uiMenuValue(i);
    if (v) {
      oled.setCursor(84, 16 + i * 10);
      oled.print(v);
    }
  }
  oled.setCursor(0, 56);
  oled.print(F("hold = back"));
}

static void drawMountPicker() {
  uint8_t m = uiMountPreview();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println(F("BEAM MOUNT"));
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  oled.setTextSize(2);
  oled.setCursor(0, 18);
  oled.print(mountName(m));
  oled.setTextSize(1);
  oled.setCursor(0, 38);
  oled.printf("beam %s", mountBeamAxis(m));
  if (m == cal.mount) {
    oled.setCursor(72, 38);
    oled.print(F("(current)"));
  }
  oled.setCursor(0, 56);
  oled.print(F("press=set hold=cancel"));
}

// Everything the guidance screen needs, gathered here so guidance.cpp stays
// free of globals and can be rendered on a host.
static void drawGuidanceScreen() {
  static char status[10];
  Angles o;
  Guidance d{};
  d.fix = orientationAngles(o);
  if (d.fix) {
    d.az = trimmedAz(o);
    d.el = o.elev;
    d.dAz = azError(d.az);
    d.dEl = cal.targetEl - o.elev;
    if (fieldSuspect(o.b)) strncpy(status, "MAG!", sizeof(status));
    else if (cal.magCal) snprintf(status, sizeof(status), "ok %.0f%%", cal.calSpread);
    else if (cal.calSpread > 0) snprintf(status, sizeof(status), "BAD %.0f%%", cal.calSpread);
    else strncpy(status, "NO CAL", sizeof(status));
  } else {
    strncpy(status, "NO FIX", sizeof(status));
  }
  status[sizeof(status) - 1] = '\0';
  d.targetAz = cal.targetAz;
  d.targetEl = cal.targetEl;
  d.status = status;
  d.net = rotctlDriving();
  drawGuidance(oled, d);
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
  // The targets moving on their own is alarming without knowing why.
  if (rotctlDriving()) {
    oled.setCursor(105, 0);
    oled.print(F("NET"));
  }
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
  // one slot answering "can I trust this azimuth". MAG! outranks the stored
  // grade, because interference now matters more than how good the fit was.
  oled.setCursor(0, 54);
  oled.print(uiEditEl() ? F("az  [EL]") : F("[AZ]  el"));
  oled.setCursor(50, 54);
  oled.print(mountName(cal.mount));
  oled.setCursor(86, 54);
  if (fieldSuspect(o.b)) {
    oled.print(F("MAG!"));
  } else if (cal.magCal) {
    oled.printf("ok %.0f%%", cal.calSpread);
  } else if (cal.calSpread > 0) {
    oled.printf("BAD %.0f%%", cal.calSpread);
  } else {
    oled.print(F("NO CAL"));
  }
}

void displayDraw(bool sensorOk) {
  if (!present) return;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  if (netOtaActive()) {
    drawOta();
    oled.display();
    return;
  }

  if (!sensorOk) {
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println(F("I2C read error"));
    oled.println();
    oled.printf("mpu6050 %s\n", sensorsProbe(MPU_ADDR) ? "ok" : "MISSING");
    oled.printf("qmc5883 %s\n", sensorsProbe(QMC_ADDR) ? "ok" : "MISSING");
  } else if (calActive()) {
    drawCalibrating();
  } else if (calResultFresh()) {
    drawCalResult();
  } else if (uiMode() == UI_MENU) {
    drawMenu();
  } else if (uiMode() == UI_MOUNT) {
    drawMountPicker();
  } else if (uiMode() == UI_WIFI) {
    drawWifi();
  } else if (uiGraphicScreen()) {
    drawGuidanceScreen();
  } else {
    drawAngles();
  }
  oled.display();
}
