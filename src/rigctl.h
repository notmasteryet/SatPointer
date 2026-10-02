// Hamlib rigctld emulation on TCP 4532, which is where gpredict's radio
// controller connects. There is no radio: the frequency gpredict tunes to,
// Doppler included, is only shown on the display.
//
//   > F 145800000       < RPRT 0
//   > f                 < 145800000
//
// Both VFOs and the split TX frequency are remembered so reads return what was
// set, which is what gpredict checks to tell whether someone turned the knob.
#pragma once

#include <Arduino.h>

void rigctlUpdate();

bool rigctlConnected();
// The frequency to show, in Hz, or 0 while nothing has been set. Main VFO
// (VFO A) - the downlink in every gpredict mode except a TX-only radio.
double rigctlFreq();
