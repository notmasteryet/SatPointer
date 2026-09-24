// Magnetometer hard/soft iron calibration by min/max over a full rotation,
// graded afterwards by how constant the corrected field magnitude comes out.
//
// |B| is the metric rather than dip because Earth's field is locally constant
// and |B| is computed from the magnetometer alone. Dip needs the
// accelerometer's idea of down too, so handling acceleration corrupts it and
// it cannot separate a bad magnetometer fit from someone waving the device.
#pragma once

#include <Arduino.h>

// Standard deviation of corrected |B| as a percentage of its mean, across the
// whole rotation. Deliberately not peak-to-peak: that is set by the two most
// extreme samples, so it measures sensor noise rather than fit quality and
// fails calibrations that are actually fine. Simulated fits give sd 0.07% when
// clean and 2.4% with heavy noise, against 5.4% for soft iron the diagonal
// model cannot represent and 16% for partial coverage - so the threshold sits
// in a real gap.
const float CAL_MAX_SPREAD_PCT = 3.0;

// How far |B| may drift from its calibration-time value before we stop
// trusting azimuth - catches interference that appeared since.
const float FIELD_WARN_PCT = 12.0;

bool calActive();
void calStart();
void calFinish();
void calCancel();  // abandon without touching stored calibration
void calFeed(const int16_t rawMag[3]);  // ignored unless active

long calSpan(uint8_t axis);
void calRange(uint8_t axis, int16_t &lo, int16_t &hi);
uint16_t calSamples();  // kept for the post-fit check, for the coverage display

// True when the live field has wandered too far from what calibration saw.
bool fieldSuspect(float b);
