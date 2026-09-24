// Rotary encoder and its push button.
#pragma once

#include <Arduino.h>

void inputBegin();
void inputUpdate();

bool inputEditEl();  // true when the dial edits elevation rather than azimuth
void inputDiag();    // print raw pin states, for the wiring check
