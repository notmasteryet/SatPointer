#include "calibration.h"

#include "config.h"
#include "orientation.h"
#include "settings.h"

static bool active = false;
static CalResult result = CAL_RESULT_NONE;
static float resultSd = 0;
static unsigned long resultAt = 0;
static const unsigned long RESULT_SHOW_MS = 10000;

CalResult calResult() { return result; }
float calResultSd() { return resultSd; }
bool calResultFresh() {
  return result != CAL_RESULT_NONE && millis() - resultAt < RESULT_SHOW_MS;
}
void calResultDismiss() { result = CAL_RESULT_NONE; }

static void setResult(CalResult r, float sd) {
  result = r;
  resultSd = sd;
  resultAt = millis();
}
static int16_t lo[3], hi[3];

// The fit is not known until the rotation ends, so grading it means keeping
// samples to re-measure afterwards. Rather than a ring buffer - which would
// only hold the last few seconds and miss most of the rotation - the buffer
// decimates as it fills, so what survives is spread evenly over the whole
// session however long it ran.
static const uint16_t CAL_CAP = 192;
static int16_t buf[CAL_CAP][3];
static uint16_t count = 0;
static uint16_t stride = 1;  // keep 1 sample in `stride`
static uint16_t phase = 0;

bool calActive() { return active; }
uint16_t calSamples() { return count; }

long calSpan(uint8_t axis) { return (long)hi[axis] - lo[axis]; }

void calRange(uint8_t axis, int16_t &outLo, int16_t &outHi) {
  outLo = lo[axis];
  outHi = hi[axis];
}

bool fieldSuspect(float b) {
  if (!cal.magCal || cal.calField <= 0) return false;
  return fabs(b - cal.calField) / cal.calField * 100.0 > FIELD_WARN_PCT;
}

void calStart() {
  for (int i = 0; i < 3; i++) {
    lo[i] = 32767;
    hi[i] = -32768;
  }
  count = 0;
  stride = 1;
  phase = 0;
  active = true;
  result = CAL_RESULT_NONE;
  Serial.println(F("# calibrating: slowly rotate through all orientations, then press or send 'c'"));
}

void calCancel() {
  active = false;
  Serial.println(F("# calibration cancelled, stored calibration unchanged"));
}

void calFeed(const int16_t rawMag[3]) {
  if (!active) return;
  for (int i = 0; i < 3; i++) {
    lo[i] = min(lo[i], rawMag[i]);
    hi[i] = max(hi[i], rawMag[i]);
  }

  if (++phase < stride) return;
  phase = 0;
  if (count == CAL_CAP) {
    // Full: throw away every other sample and take half as often from here on.
    for (uint16_t i = 0; i < CAL_CAP / 2; i++) {
      for (int k = 0; k < 3; k++) buf[i][k] = buf[i * 2][k];
    }
    count = CAL_CAP / 2;
    stride *= 2;
  }
  for (int k = 0; k < 3; k++) buf[count][k] = rawMag[k];
  count++;
}

// Re-measures |B| across the kept samples using the fit just computed. A good
// fit maps the samples onto a sphere, so |B| comes out constant; whatever
// variation remains is what the fit failed to remove. Reported as a standard
// deviation rather than a range - see CAL_MAX_SPREAD_PCT.
static float gradeFit(float &meanOut) {
  float sum = 0, sumSq = 0;
  for (uint16_t i = 0; i < count; i++) {
    float m[3];
    for (int k = 0; k < 3; k++) m[k] = (buf[i][k] - cal.offset[k]) * cal.scale[k];
    float b = sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]) / QMC_LSB_PER_UT;
    sum += b;
    sumSq += b * b;
  }
  meanOut = sum / count;
  if (meanOut <= 0) return 999.0;
  float var = sumSq / count - meanOut * meanOut;
  return sqrt(var < 0 ? 0 : var) / meanOut * 100.0;
}

void calFinish() {
  active = false;

  float r[3], avg = 0;
  for (int i = 0; i < 3; i++) {
    r[i] = (hi[i] - (float)lo[i]) / 2;
    if (r[i] < 300) {
      Serial.printf("# calibration failed: axis %c barely moved, rotate further\n", 'X' + i);
      setResult(CAL_RESULT_NO_ROTATION, 0);
      return;
    }
    avg += r[i] / 3;
  }
  if (count < 20) {
    Serial.println(F("# calibration failed: too few samples to grade, rotate for longer"));
    setResult(CAL_RESULT_NO_SAMPLES, 0);
    return;
  }

  for (int i = 0; i < 3; i++) {
    cal.offset[i] = ((long)hi[i] + lo[i]) / 2;  // hard iron
    cal.scale[i] = avg / r[i];                  // simple soft iron
  }

  float mean;
  float spread = gradeFit(mean);
  cal.calField = mean;
  cal.calSpread = spread;
  cal.magCal = spread <= CAL_MAX_SPREAD_PCT;
  setResult(cal.magCal ? CAL_RESULT_OK : CAL_RESULT_POOR, spread);

  settingsSave();
  // The correction changed under the filter, so its contents are stale.
  orientationReset();

  Serial.printf("# calibration %s: |B| %.1f uT, sd %.2f%% over %u samples\n",
                cal.magCal ? "OK" : "POOR", mean, spread, count);
  if (!cal.magCal) {
    Serial.printf("# sd above %.0f%% - rotate more slowly and cover every\n",
                  CAL_MAX_SPREAD_PCT);
    Serial.println(F("# orientation, and calibrate with cables exactly as they will sit"));
  } else {
    Serial.println(F("# point the beam at a known bearing and send nDEG"));
  }
  settingsPrint();
}
