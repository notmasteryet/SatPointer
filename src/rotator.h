// Tells the SatNOGS rotator where its antenna is pointing, using this device
// clamped to the boom as the reference. The same job as tracker/'s
// `sattrack.py deploy`, with the pointer standing in for sighting the sun:
//
//   M114                      what the rotator thought, for the record
//   M503 / M564 E             which side its E axis is set for; flip if wrong
//   G92 A<az> E<el>           declare the current position
//   M208 S1 / S0 ...          re-centre the travel a turn either way
//   M500                      keep it
//   M114                      confirm it took
//
// Re-centring the travel assumes the cable wrap is neutral right now - the
// rotator's fence is a wrap budget around the declared position, exactly as
// deploy treats it.
//
// The antenna can be bolted to either end of the elevation shaft. On the far
// end it faces the other way, so the same rotation lowers it instead of
// raising it. "flipped" (CCW on the screen; CW is as built) says it is
// there: the rotator's E axis is then
// inverted with M564 E (only if it is not already), which keeps negative E
// meaning up on both sides, so tracker/'s el_sign holds either way. The 180
// degrees of azimuth that also come with the far side need nothing - the
// declaration is the pointer's reading of where the antenna really points.
//
// Nothing moves: G92, M208, M564 and M500 do not need the motors armed.
#pragma once

#include <Arduino.h>

enum RotatorState : uint8_t { ROT_IDLE, ROT_BUSY, ROT_DONE, ROT_FAILED };

void rotatorUpdate();

// Starts the job with the beam's true azimuth and sky elevation. Fails at once
// (state ROT_FAILED) if there is no network to reach the rotator on.
void rotatorDeclare(float az, float skyEl, bool flipped);
void rotatorDismiss();  // DONE or FAILED back to IDLE

RotatorState rotatorState();
const char *rotatorMessage();  // the current step, or why it failed

// What was declared, and what the rotator reported beforehand. Axis
// coordinates, as the rotator counts them.
float rotatorDeclaredA();
float rotatorDeclaredE();
float rotatorPreviousA();
float rotatorPreviousE();
bool rotatorWasReferenced();
bool rotatorFlippedE();  // this run sent M564 E
