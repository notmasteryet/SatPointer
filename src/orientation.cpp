#include "orientation.h"

#include "config.h"
#include "settings.h"

// Sensor axes expressed in the board frame (forward, left, up):
// +1/-1 = sensor X, +2/-2 = sensor Y, +3/-3 = sensor Z.
static const int8_t ACC_AXIS[3] = { +1, +2, +3 };  // GY-521: X forward, Y left
static const int8_t MAG_AXIS[3] = { +2, -1, +3 };  // GY-271: Y forward, X right

// Which board axis the beam points along for each way of clamping the device,
// plus a reference "up" perpendicular to it that fixes roll. Same +-1/2/3
// encoding as the axis tables above.
struct Mount {
  const char *name;
  const char *beamLabel;
  int8_t beam;
  int8_t up;
};

static const Mount MOUNTS[] = {
  { "top",   "+Y", +2, +3 },  // beam out the left face
  { "left",  "-X", -1, +3 },  // beam out the back
  { "right", "+X", +1, +3 },  // beam out the front
  { "down",  "-Z", -3, +1 },  // beam out the bottom, screen faces up
};
static const uint8_t MOUNT_COUNT = sizeof(MOUNTS) / sizeof(MOUNTS[0]);
static const uint8_t MOUNT_RIGHT = 2;  // +X, the orientation the POC ran in

uint8_t mountCount() { return MOUNT_COUNT; }
uint8_t mountDefault() { return MOUNT_RIGHT; }

const char *mountName(uint8_t i) {
  return MOUNTS[i < MOUNT_COUNT ? i : MOUNT_RIGHT].name;
}

const char *mountBeamAxis(uint8_t i) {
  return MOUNTS[i < MOUNT_COUNT ? i : MOUNT_RIGHT].beamLabel;
}

// ---- vector math ----
static void cross(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

static float dot(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float norm(const float v[3]) {
  return sqrt(dot(v, v));
}

static bool normalize(float v[3]) {
  float n = norm(v);
  if (n < 1e-6) return false;
  for (int i = 0; i < 3; i++) v[i] /= n;
  return true;
}

static float mapAxis(const float v[3], int8_t s) {
  float x = v[abs(s) - 1];
  return s < 0 ? -x : x;
}

static void axisVec(int8_t s, float v[3]) {
  v[0] = v[1] = v[2] = 0;
  v[abs(s) - 1] = s < 0 ? -1.0 : 1.0;
}

// Board frame -> canonical frame, where the beam is forward. With the "right"
// mount (beam +X, up +Z) this is the identity, so the POC's behaviour is
// preserved exactly.
static void toCanonical(const float in[3], float out[3]) {
  const Mount &m = MOUNTS[cal.mount < MOUNT_COUNT ? cal.mount : MOUNT_RIGHT];
  float fwd[3], up[3], left[3];
  axisVec(m.beam, fwd);
  axisVec(m.up, up);
  cross(up, fwd, left);  // forward x left = up, so left = up x forward
  out[0] = dot(in, fwd);
  out[1] = dot(in, left);
  out[2] = dot(in, up);
}

// ---- filtered state, canonical frame ----
static float acc[3], mag[3];
static bool haveSample = false;

void orientationReset() { haveSample = false; }
bool orientationReady() { return haveSample; }

void orientationUpdate(const int16_t rawAcc[3], const int16_t rawMag[3]) {
  float a[3], m[3];
  for (int i = 0; i < 3; i++) {
    a[i] = rawAcc[i];
    m[i] = (rawMag[i] - cal.offset[i]) * cal.scale[i];
  }

  float ab[3], mb[3];  // board frame
  for (int i = 0; i < 3; i++) {
    ab[i] = mapAxis(a, ACC_AXIS[i]);
    mb[i] = mapAxis(m, MAG_AXIS[i]);
  }

  float ac[3], mc[3];  // canonical frame
  toCanonical(ab, ac);
  toCanonical(mb, mc);

  for (int i = 0; i < 3; i++) {
    if (haveSample) {
      acc[i] += ALPHA * (ac[i] - acc[i]);
      mag[i] += ALPHA * (mc[i] - mag[i]);
    } else {
      acc[i] = ac[i];
      mag[i] = mc[i];
    }
  }
  haveSample = true;
}

// Azimuth without trim.
bool orientationAngles(Angles &o) {
  if (!haveSample) return false;

  // Down, East, North in the canonical frame (accelerometer reads "up" at rest)
  float D[3] = { -acc[0], -acc[1], -acc[2] };
  float E[3], N[3];
  if (!normalize(D)) return false;
  cross(D, mag, E);
  if (!normalize(E)) return false;
  cross(E, D, N);

  // The beam is (1,0,0) here, so its N/E/D components are the first elements
  o.elev = asin(constrain(-D[0], -1.0, 1.0)) * RAD_TO_DEG;
  o.az = atan2(E[0], N[0]) * RAD_TO_DEG;
  o.roll = atan2(acc[1], acc[2]) * RAD_TO_DEG;
  float b = norm(mag);
  o.b = b / QMC_LSB_PER_UT;
  o.dip = asin(constrain(dot(mag, D) / b, -1.0, 1.0)) * RAD_TO_DEG;
  return true;
}

float trimmedAz(const Angles &o) {
  return fmod(o.az + cal.azTrim + 720.0, 360.0);
}

// Shortest signed way round: positive means turn clockwise (right).
float azError(float current) {
  return fmod(cal.targetAz - current + 540.0, 360.0) - 180.0;
}
