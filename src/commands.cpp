#include "commands.h"

#include "calibration.h"
#include "input.h"
#include "orientation.h"
#include "sensors.h"
#include "settings.h"

static bool rawMode = false;

void commandsHelp() {
  Serial.println(F("# SatPointer: c=calibrate nDEG=set bearing m=mount r=raw"));
  Serial.println(F("#             e=encoder p=print z=erase h=help"));
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

// Cycles rather than taking an index: there are only four, and a wrong digit
// on a 4-item list is more annoying than one extra press.
static void nextMount() {
  cal.mount = (cal.mount + 1) % mountCount();
  settingsSave();
  // The filter holds vectors in the old frame; blending them across a mount
  // change would drift through nonsense for a second or so.
  orientationReset();
  Serial.printf("# mount %s (beam %s) - recalibrate, the beam's metal moved\n",
                mountName(cal.mount), mountBeamAxis(cal.mount));
}

void commandsUpdate() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'c': calActive() ? calFinish() : calStart(); break;
      // The ESP8266 core's Stream has no LookaheadMode overload; the plain
      // parseFloat() already skips leading non-numeric characters.
      case 'n': setBearing(Serial.parseFloat()); break;
      case 'm': nextMount(); break;
      case 'r': rawMode = !rawMode; break;
      case 'p': settingsPrint(); break;
      case 'e': inputDiag(); break;
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
