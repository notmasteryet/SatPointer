// SSD1306 OLED output.
#pragma once

#include <Arduino.h>

bool displayBegin();    // false if nothing answers at the OLED address
bool displayPresent();
void displaySplash();
void displayDraw(bool sensorOk);
