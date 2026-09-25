// The aiming screen, drawn rather than written.
//
// Takes a plain struct and an Adafruit_GFX rather than reading globals, so the
// exact code that ships can be compiled against a mock and rendered on a host.
// Layout bugs on a 128x64 panel are otherwise only findable by flashing and
// squinting, and nobody reviewing this can see the screen.
#pragma once

#include <Adafruit_GFX.h>

struct Guidance {
  bool fix;
  float az, el;          // where the beam points now
  float targetAz, targetEl;
  float dAz, dEl;        // how far to go; dAz positive means turn clockwise
  const char *status;    // cal state, shown bottom right
  bool net;              // a gpredict client is driving the target
};

void drawGuidance(Adafruit_GFX &g, const Guidance &d);
