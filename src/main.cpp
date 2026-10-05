// SatPointer - elevation and azimuth readout for aiming a satellite dish.
// ESP-12F + GY-521 (MPU6050 @0x68) + GY-271 (QMC5883L @0x0D) + SSD1306 @0x3C
// + EC11 rotary encoder.
//
// Wiring, pin assignments and the mount table are documented in config.h,
// orientation.h and the README. The ESP8266 is NOT 5V tolerant - the sensor
// modules run from 3.3V.

#include <Arduino.h>

#include "calibration.h"
#include "commands.h"
#include "config.h"
#include "display.h"
#include "input.h"
#include "net.h"
#include "ui.h"
#include "orientation.h"
#include "rigctl.h"
#include "rotator.h"
#include "rotctl.h"
#include "sensors.h"
#include "settings.h"

static int16_t rawAcc[3], rawMag[3];
static bool sensorOk = false;
static unsigned long lastSample, lastPrint, lastDraw;

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);  // for nDEG
  delay(200);
  Serial.println();
  Serial.println(F("# SatPointer starting"));

  inputBegin();
  sensorsBegin();
  sensorsScanReport();

  Serial.printf("# ssd1306 %s\n", displayBegin() ? "found" : "MISSING");
  displaySplash();

  settingsBegin();
  netBegin();
  commandsHelp();
  settingsPrint();
}

void loop() {
  netUpdate();
  rotctlUpdate();
  rigctlUpdate();
  rotatorUpdate();
  commandsUpdate();
  uiUpdate();

  unsigned long now = millis();

  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;
    sensorOk = sensorsRead(rawAcc, rawMag);
    if (sensorOk) {
      calFeed(rawMag);
      orientationUpdate(rawAcc, rawMag);
    }
  }
  if (orientationReady() && now - lastPrint >= PRINT_MS) {
    lastPrint = now;
    commandsReport(sensorOk, rawAcc, rawMag);
  }
  if (orientationReady() && now - lastDraw >= DRAW_MS) {
    lastDraw = now;
    displayDraw(sensorOk);
  }
  yield();
}
