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

// Measured with the 'a' command, not inferred: held with the top face north
// and the screen up, +X read north, +Y west, -Y east and -Z straight down. An
// earlier version of this table was guessed from the board layout and had
// three of the four entries wrong.
static const Mount MOUNTS[] = {
  { "top",   "+X", +1, +3 },  // beam out the top face, screen up
  { "left",  "+Y", +2, +3 },
  { "right", "-Y", -2, +3 },
  { "away",  "-Z", -3, +1 },  // beam out the back, screen up
};
static const uint8_t MOUNT_COUNT = sizeof(MOUNTS) / sizeof(MOUNTS[0]);
// The beam axis the POC was verified in, which is the top face.
static const uint8_t MOUNT_TOP = 0;

uint8_t mountCount() { return MOUNT_COUNT; }
uint8_t mountDefault() { return MOUNT_TOP; }

const char *mountName(uint8_t i) {
  return MOUNTS[i < MOUNT_COUNT ? i : MOUNT_TOP].name;
}

const char *mountBeamAxis(uint8_t i) {
  return MOUNTS[i < MOUNT_COUNT ? i : MOUNT_TOP].beamLabel;
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

// ---- filtered state, board frame ----
// Filtering in the board frame rather than the beam frame means a mount change
// does not invalidate the filter: the mount only affects which direction we
// ask about, not what the sensors measured.
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

  for (int i = 0; i < 3; i++) {
    float av = mapAxis(a, ACC_AXIS[i]);
    float mv = mapAxis(m, MAG_AXIS[i]);
    if (haveSample) {
      acc[i] += ALPHA * (av - acc[i]);
      mag[i] += ALPHA * (mv - mag[i]);
    } else {
      acc[i] = av;
      mag[i] = mv;
    }
  }
  haveSample = true;
}

// Down, East and North as unit vectors in the board frame. The accelerometer
// reads "up" at rest, hence the negation.
static bool localFrame(float D[3], float E[3], float N[3]) {
  if (!haveSample) return false;
  D[0] = -acc[0];
  D[1] = -acc[1];
  D[2] = -acc[2];
  if (!normalize(D)) return false;
  cross(D, mag, E);
  if (!normalize(E)) return false;
  cross(E, D, N);
  return true;
}

bool orientationAxis(int8_t axis, float &az, float &elev) {
  float D[3], E[3], N[3], f[3];
  if (!localFrame(D, E, N)) return false;
  axisVec(axis, f);
  elev = asin(constrain(-dot(D, f), -1.0, 1.0)) * RAD_TO_DEG;
  az = fmod(atan2(dot(E, f), dot(N, f)) * RAD_TO_DEG + 360.0, 360.0);
  return true;
}

// Azimuth without trim.
bool orientationAngles(Angles &o) {
  float D[3], E[3], N[3];
  if (!localFrame(D, E, N)) return false;

  const Mount &mt = MOUNTS[cal.mount < MOUNT_COUNT ? cal.mount : MOUNT_TOP];
  float f[3], up[3], left[3];
  axisVec(mt.beam, f);
  axisVec(mt.up, up);
  cross(up, f, left);  // beam x left = up, so left = up x beam

  o.elev = asin(constrain(-dot(D, f), -1.0, 1.0)) * RAD_TO_DEG;
  o.az = atan2(dot(E, f), dot(N, f)) * RAD_TO_DEG;
  // Roll about the beam, measured from the mount's reference up.
  o.roll = atan2(dot(acc, left), dot(acc, up)) * RAD_TO_DEG;

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
