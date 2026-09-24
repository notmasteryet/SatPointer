// Persisted configuration, stored in the ESP8266's EEPROM flash shadow.
#pragma once

#include <Arduino.h>

struct Settings {
  uint16_t magic;
  bool magCal;
  int16_t offset[3];  // hard iron, sensor frame
  float scale[3];     // soft iron, sensor frame
  float azTrim;       // declination + mounting error, set with 'n'
  float targetAz;     // what we are aiming at, set with the encoder
  float targetEl;
  uint8_t mount;      // index into the mount table in orientation.cpp
};

extern Settings cal;

void settingsBegin();
void settingsSave();
void settingsReset();
void settingsPrint();
