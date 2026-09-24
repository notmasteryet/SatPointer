#include "settings.h"

#include <EEPROM.h>

#include "calibration.h"
#include "net.h"
#include "orientation.h"

// ESP8266 EEPROM is a RAM shadow of a flash sector: it needs begin() and
// commit(), unlike the AVR version this started life on.
// Must exceed sizeof(Settings); the WiFi credentials alone are 98 bytes, which
// overflowed the previous 128.
static const size_t EEPROM_SIZE = 256;

// Bumped whenever the struct layout changes, so an older layout in flash is
// discarded rather than read as garbage. 0xCA1D added targetAz/targetEl,
// 0xCA1E added mount, 0xCA1F the calibration grade, 0xCA20 the WiFi fields.
static const uint16_t SETTINGS_MAGIC = 0xCA20;

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
  cal.wifiOn = true;
  cal.ssid[0] = '\0';
  cal.pass[0] = '\0';
}

// A magic bump is only as good as remembering to do it: forgetting one lets
// the old bytes be read at the new field offsets, which put NaN into the
// targets and poisoned every derived number. Cheap insurance against that and
// against a genuinely corrupted cell.
static bool badFloat(float f) { return isnan(f) || isinf(f); }

static bool settingsSane() {
  if (badFloat(cal.azTrim) || badFloat(cal.targetAz) || badFloat(cal.targetEl)) return false;
  if (badFloat(cal.calField) || badFloat(cal.calSpread)) return false;
  for (int i = 0; i < 3; i++) {
    if (badFloat(cal.scale[i]) || cal.scale[i] <= 0.1 || cal.scale[i] > 10.0) return false;
  }
  if (cal.mount >= mountCount()) return false;
  // Unterminated strings would run off the end of the struct in every printf
  // and WiFi.begin() that touches them.
  if (cal.ssid[sizeof(cal.ssid) - 1] != '\0') return false;
  if (cal.pass[sizeof(cal.pass) - 1] != '\0') return false;
  return true;
}

void settingsBegin() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, cal);
  if (cal.magic != SETTINGS_MAGIC) {
    settingsReset();
  } else if (!settingsSane()) {
    Serial.println(F("# stored settings failed a sanity check, resetting"));
    settingsReset();
  }
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
  Serial.printf("# wifi %s, ssid '%s', %s %s\n", cal.wifiOn ? "on" : "off", cal.ssid,
                netStateName(), netAddress().c_str());
  Serial.print(F("# target az "));
  Serial.print(cal.targetAz, 1);
  Serial.print(F(" el "));
  Serial.println(cal.targetEl, 1);
}
