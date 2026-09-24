// Sensor fusion: raw accelerometer + magnetometer -> azimuth / elevation.
//
// Three frames are involved:
//   sensor    - what the chips report, per their printed axes
//   board     - X = GY-521 +X arrow = forward, Y = left, Z = up
//   canonical - the beam is +X, left is +Y, up is +Z
//
// The mount setting says which board axis the beam points along. Rotating into
// the canonical frame first means the fusion maths never has to know about it.
#pragma once

#include <Arduino.h>

struct Angles {
  float elev, az, roll, dip, b;
};

// ---- mount table ----
uint8_t mountCount();
uint8_t mountDefault();
const char *mountName(uint8_t i);
const char *mountBeamAxis(uint8_t i);  // "+X", "-Z", ... for display

// ---- fusion ----
void orientationReset();  // drop filter state, e.g. after a mount change
bool orientationReady();
void orientationUpdate(const int16_t rawAcc[3], const int16_t rawMag[3]);
bool orientationAngles(Angles &o);

// Azimuth and elevation of one board axis (+-1/2/3 = +-X/Y/Z), whatever the
// mount is set to. Used to work out which board axis corresponds to which
// physical face of the device, by pointing a known face at a known bearing.
bool orientationAxis(int8_t axis, float &az, float &elev);

float trimmedAz(const Angles &o);

// "the beam is pointing at this true bearing right now" - stores the
// difference as the trim, absorbing declination and mounting error together.
bool orientationSetBearing(float trueBearing);
float azError(float current);
