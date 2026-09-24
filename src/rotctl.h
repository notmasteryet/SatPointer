// Hamlib rotctld emulation on TCP 4533, which is what gpredict drives
// natively. You are the rotator: gpredict sends where to point and reads back
// where the device actually points, and you turn the dish by hand following
// the display.
//
//   > p                 < 90.2
//                       < -1.5
//   > P 145.0 28.5      < RPRT 0
//
// Targets arriving over the network are deliberately NOT persisted. gpredict
// sends P continuously while tracking a pass, and the encoder's save path
// would turn one pass into thousands of flash writes.
#pragma once

#include <Arduino.h>

void rotctlUpdate();

bool rotctlConnected();
bool rotctlDriving();  // a network target arrived recently
