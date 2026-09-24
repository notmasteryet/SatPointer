// I2C bus plus the MPU6050 and QMC5883L raw reads.
#pragma once

#include <Arduino.h>

void sensorsBegin();
bool sensorsProbe(uint8_t addr);

// Walks the bus and names what it finds. Also reports addresses we do not
// expect, which is how a GY-271 carrying an HMC5883L (0x1E) or QMC5883P
// (0x2C) instead of a QMC5883L gives itself away.
void sensorsScanReport();

bool sensorsRead(int16_t acc[3], int16_t mag[3]);
