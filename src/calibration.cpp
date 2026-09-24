#include "calibration.h"

#include "orientation.h"
#include "settings.h"

static bool active = false;
static int16_t lo[3], hi[3];

bool calActive() { return active; }

long calSpan(uint8_t axis) { return (long)hi[axis] - lo[axis]; }

void calRange(uint8_t axis, int16_t &outLo, int16_t &outHi) {
  outLo = lo[axis];
  outHi = hi[axis];
}

void calStart() {
  for (int i = 0; i < 3; i++) {
    lo[i] = 32767;
    hi[i] = -32768;
  }
  active = true;
  Serial.println(F("# calibrating: slowly rotate the board through all orientations, then send 'c'"));
}

void calFeed(const int16_t rawMag[3]) {
  if (!active) return;
  for (int i = 0; i < 3; i++) {
    lo[i] = min(lo[i], rawMag[i]);
    hi[i] = max(hi[i], rawMag[i]);
  }
}

void calFinish() {
  active = false;
  float r[3], avg = 0;
  for (int i = 0; i < 3; i++) {
    r[i] = (hi[i] - (float)lo[i]) / 2;
    if (r[i] < 300) {
      Serial.println(F("# calibration failed: not enough rotation, try again"));
      return;
    }
    avg += r[i] / 3;
  }
  cal.magCal = true;
  for (int i = 0; i < 3; i++) {
    cal.offset[i] = ((long)hi[i] + lo[i]) / 2;  // hard iron
    cal.scale[i] = avg / r[i];                  // simple soft iron
  }
  settingsSave();
  // The correction changed under the filter, so its contents are stale.
  orientationReset();
  Serial.println(F("# calibration saved; point the beam at a known bearing and send nDEG"));
  settingsPrint();
}
