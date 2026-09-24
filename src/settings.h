// Persisted configuration, stored in the ESP8266's EEPROM flash shadow.
#pragma once

#include <Arduino.h>

struct Settings {
  uint16_t magic;
  bool magCal;
  int16_t offset[3];  // hard iron, sensor frame
  float scale[3];     // soft iron, sensor frame
  float azTrim;       // declination + mounting error, set with 'n'
  float calField;     // |B| in uT that calibration saw, for drift detection
  float calSpread;    // sd of |B| after the fit, as %% of mean; lower is better
  float targetAz;     // what we are aiming at, set with the encoder
  float targetEl;
  uint8_t mount;      // index into the mount table in orientation.cpp
  bool wifiOn;        // radio enabled; off is a legitimate operating mode here
  char ssid[33];
  char pass[65];
};

extern Settings cal;

void settingsBegin();
void settingsSave();
void settingsReset();
void settingsPrint();
