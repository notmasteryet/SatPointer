#include "settings.h"

#include <EEPROM.h>

#include "calibration.h"
#include "orientation.h"

// ESP8266 EEPROM is a RAM shadow of a flash sector: it needs begin() and
// commit(), unlike the AVR version this started life on.
static const size_t EEPROM_SIZE = 128;

// Bumped whenever the struct layout changes, so an older layout in flash is
// discarded rather than read as garbage. 0xCA1D added targetAz/targetEl,
// 0xCA1E added mount.
static const uint16_t SETTINGS_MAGIC = 0xCA1E;

Settings cal;

void settingsReset() {
  cal.magic = SETTINGS_MAGIC;
  cal.magCal = false;
  for (int i = 0; i < 3; i++) {
    cal.offset[i] = 0;
    cal.scale[i] = 1.0;
  }
  cal.azTrim = 0;
  cal.calField = 0;
  cal.calSpread = 0;
  cal.targetAz = 0;
  cal.targetEl = 0;
  cal.mount = mountDefault();
}

void settingsBegin() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, cal);
  if (cal.magic != SETTINGS_MAGIC) settingsReset();
  // A stored index from a future build, or a corrupted byte, would index off
  // the end of the mount table on every sample.
  if (cal.mount >= mountCount()) cal.mount = mountDefault();
}

void settingsSave() {
  EEPROM.put(0, cal);
  if (!EEPROM.commit()) Serial.println(F("# EEPROM commit failed"));
}

void settingsPrint() {
  if (cal.magCal) {
    Serial.print(F("# cal offset "));
    for (int i = 0; i < 3; i++) { Serial.print(cal.offset[i]); Serial.print(' '); }
    Serial.print(F(" scale "));
    for (int i = 0; i < 3; i++) { Serial.print(cal.scale[i], 3); Serial.print(' '); }
    Serial.println();
    Serial.printf("# cal |B| %.1f uT, sd %.2f%%\n", cal.calField, cal.calSpread);
  } else if (cal.calSpread > 0) {
    Serial.printf("# calibration POOR: sd %.2f%% (want <= %.0f%%), redo it\n",
                  cal.calSpread, CAL_MAX_SPREAD_PCT);
  } else {
    Serial.println(F("# magnetometer not calibrated, send 'c' to calibrate"));
  }
  Serial.print(F("# azimuth trim "));
  Serial.println(cal.azTrim, 1);
  Serial.printf("# mount %s (beam %s)\n", mountName(cal.mount), mountBeamAxis(cal.mount));
  Serial.print(F("# target az "));
  Serial.print(cal.targetAz, 1);
  Serial.print(F(" el "));
  Serial.println(cal.targetEl, 1);
}
