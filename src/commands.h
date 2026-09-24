// Serial command interface and the periodic data line.
#pragma once

#include <Arduino.h>

void commandsHelp();
void commandsUpdate();
void commandsReport(bool sensorOk, const int16_t rawAcc[3], const int16_t rawMag[3]);
