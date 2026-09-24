#include "commands.h"

#include "calibration.h"
#include "input.h"
#include "net.h"
#include "orientation.h"
#include "sensors.h"
#include "settings.h"
#include "ui.h"

static bool rawMode = false;

void commandsHelp() {
  Serial.println(F("# SatPointer: c=calibrate x=cancel-cal nDEG=set bearing m=mount"));
  Serial.println(F("#             w<ssid> <pass>=wifi W=radio r=raw e=encoder a=axes"));
  Serial.println(F("#             p=print z=erase h=help"));
}

static void setBearing(float bearing) {
  Angles o;
  if (!orientationAngles(o)) {
    Serial.println(F("# no valid reading"));
    return;
  }
  cal.azTrim = fmod(bearing - o.az + 540.0, 360.0) - 180.0;
  settingsSave();
  Serial.print(F("# bearing set to "));
  Serial.println(bearing, 1);
  settingsPrint();
}

void commandsUpdate() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'c': calActive() ? calFinish() : calStart(); break;
      case 'x': if (calActive()) calCancel(); break;
      // The ESP8266 core's Stream has no LookaheadMode overload; the plain
      // parseFloat() already skips leading non-numeric characters.
      case 'n': setBearing(Serial.parseFloat()); break;
      // Cycles rather than taking an index: only four, and a wrong digit on a
      // 4-item list is more annoying than one extra press.
      case 'm': uiSetMount((cal.mount + 1) % mountCount()); break;
      // "w<ssid> <password>" - the rest of the line, so an SSID with spaces
      // still works as long as the password has none.
      case 'w': {
        String line = Serial.readStringUntil('\n');
        line.trim();
        int sp = line.lastIndexOf(' ');
        if (sp < 0) netSetCredentials(line.c_str(), "");
        else netSetCredentials(line.substring(0, sp).c_str(), line.substring(sp + 1).c_str());
        Serial.println(F("# reboot or toggle wifi in the menu to apply"));
        break;
      }
      // Capital W toggles the radio. Separate from the menu toggle so the
      // interference it causes can be measured without a hand near the
      // magnetometer, which would confound the measurement.
      case 'W':
        netSetEnabled(!cal.wifiOn);
        Serial.printf("# wifi %s\n", cal.wifiOn ? "on" : "off");
        break;
      case 'r': rawMode = !rawMode; break;
      case 'p': settingsPrint(); break;
      case 'e': inputDiag(); break;
      // Names every board axis by where it is currently pointing. Point a
      // known face of the device at a known bearing and this says which axis
      // that face is, which is the only reliable way to fill in the mount
      // table - deriving it from how the board looks has already been wrong.
      case 'a': {
        static const int8_t AXES[6] = { +1, -1, +2, -2, +3, -3 };
        static const char *const NAMES[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
        for (int i = 0; i < 6; i++) {
          float az, el;
          if (orientationAxis(AXES[i], az, el)) {
            Serial.printf("# axis %s  az %5.1f  elev %5.1f\n", NAMES[i], az, el);
          }
        }
        break;
      }
      case 'z':
        settingsReset();
        settingsSave();
        orientationReset();
        Serial.println(F("# settings erased"));
        break;
      case 'h':
      case '?': commandsHelp(); break;
    }
  }
}

void commandsReport(bool sensorOk, const int16_t rawAcc[3], const int16_t rawMag[3]) {
  if (!sensorOk) {
    Serial.println(F("# I2C read error"));
    return;
  }
  if (rawMode) {
    Serial.printf("ax:%d ay:%d az:%d mx:%d my:%d mz:%d\n", rawAcc[0], rawAcc[1],
                  rawAcc[2], rawMag[0], rawMag[1], rawMag[2]);
    return;
  }
  if (calActive()) {
    Serial.print(F("# range"));
    for (int i = 0; i < 3; i++) {
      int16_t lo, hi;
      calRange(i, lo, hi);
      Serial.print(' '); Serial.print(lo);
      Serial.print(F("..")); Serial.print(hi);
    }
    Serial.println();
    return;
  }

  Angles o;
  if (!orientationAngles(o)) return;
  float az = trimmedAz(o);

  Serial.print(F("elev:")); Serial.print(o.elev, 1);
  Serial.print(F(" az:")); Serial.print(az, 1);
  Serial.print(F(" daz:")); Serial.print(azError(az), 1);
  Serial.print(F(" del:")); Serial.print(cal.targetEl - o.elev, 1);
  Serial.print(F(" roll:")); Serial.print(o.roll, 1);
  Serial.print(F(" dip:")); Serial.print(o.dip, 1);
  Serial.print(F(" B:")); Serial.println(o.b, 1);
}
