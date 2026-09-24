// Magnetometer hard/soft iron calibration by min/max over a full rotation.
#pragma once

#include <Arduino.h>

bool calActive();
void calStart();
void calFinish();
void calCancel();  // abandon without touching stored calibration
void calFeed(const int16_t rawMag[3]);  // ignored unless active

long calSpan(uint8_t axis);
void calRange(uint8_t axis, int16_t &lo, int16_t &hi);
