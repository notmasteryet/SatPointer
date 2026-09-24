// Rotary encoder and its push button, as events. What they mean is decided in
// ui.cpp, which is the only place that knows what mode we are in.
#pragma once

#include <Arduino.h>

enum class InputEvent : uint8_t { None, ShortPress, LongPress };

void inputBegin();

int8_t inputTakeDetents();       // signed detents since the last call, then zeroed
InputEvent inputTakeEvent();     // pending button event, then cleared
void inputPoll();                // must run often; times the long press

void inputDiag();  // print raw pin states, for the wiring check
