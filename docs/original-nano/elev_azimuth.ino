// elev_azimuth - elevation and azimuth of a breadboard
// Arduino Nano + GY-521 (MPU6050 @0x68) + GY-271 (QMC5883L @0x0D), no libraries.
//
// Wiring (both modules): VCC->5V, GND->GND, SDA->A4, SCL->A5
//
// Board frame: X = forward = GY-521 +X arrow (= GY-271 +Y arrow),
//              Y = left = GY-521 +Y arrow (towards the Nano), Z = up.
// Angles like a satellite dish pointing along forward:
//   elev = angle of forward above the horizon (-90..+90)
//   az   = true bearing of forward (0..360, 0 = north, 90 = east, clockwise)
//   roll = rotation around forward, right side down positive
//   dip  = magnetic inclination; should stay constant however you turn the board
//   B    = field strength in uT; Earth is 25..65, much more means interference
//
// Serial 115200, commands:
//   c  start / finish magnetometer calibration (slowly rotate board in all directions)
//   nDEG  forward currently points to true bearing DEG (e.g. n90 = east, n = north):
//         stores azimuth trim (declination + mounting error)
//   r  toggle raw sensor output (sensor frames)
//   p  print stored settings
//   z  erase settings
//   h  help

#include <Wire.h>
#include <EEPROM.h>

// ---- configuration ----
// Sensor axes expressed in the board frame (forward, left, up):
// +1/-1 = sensor X, +2/-2 = sensor Y, +3/-3 = sensor Z.
const int8_t ACC_AXIS[3] = { +1, +2, +3 };  // GY-521: X forward, Y left
const int8_t MAG_AXIS[3] = { +2, -1, +3 };  // GY-271: Y forward, X right

const uint8_t MPU_ADDR = 0x68;
const uint8_t QMC_ADDR = 0x0D;
const float ALPHA = 0.2;             // low-pass factor per sample, lower = smoother
const unsigned long SAMPLE_MS = 20;  // 50 Hz
const unsigned long PRINT_MS = 100;  // 10 Hz
const float QMC_LSB_PER_UT = 30.0;   // +-8 G range: 3000 LSB/G

struct Settings {
  uint16_t magic;
  bool magCal;
  int16_t offset[3];
  float scale[3];
  float azTrim;  // declination + mounting error, set with 'n'
};
const uint16_t SETTINGS_MAGIC = 0xCA1C;
Settings cal;

struct Angles {
  float elev, az, roll, dip, b;
};

int16_t rawAcc[3], rawMag[3];
float acc[3], mag[3];  // filtered, board frame
bool haveSample = false, sensorOk = false, rawMode = false, calibrating = false;
int16_t calMin[3], calMax[3];
unsigned long lastSample, lastPrint;

// ---- I2C ----
void writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool readBytes(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

bool readAccel(int16_t a[3]) {
  uint8_t b[6];
  if (!readBytes(MPU_ADDR, 0x3B, b, 6)) return false;  // X,Y,Z big-endian
  for (int i = 0; i < 3; i++) a[i] = (int16_t)((uint16_t)b[2 * i] << 8 | b[2 * i + 1]);
  return true;
}

bool readMag(int16_t m[3]) {
  uint8_t b[7];
  if (!readBytes(QMC_ADDR, 0x00, b, 7)) return false;  // X,Y,Z little-endian + status
  if (b[6] & 0x02) return false;                        // overflow
  for (int i = 0; i < 3; i++) m[i] = (int16_t)((uint16_t)b[2 * i + 1] << 8 | b[2 * i]);
  return true;
}

// ---- vector math ----
void cross(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

float norm(const float v[3]) {
  return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

bool normalize(float v[3]) {
  float n = norm(v);
  if (n < 1e-6) return false;
  for (int i = 0; i < 3; i++) v[i] /= n;
  return true;
}

float mapAxis(const float v[3], int8_t s) {
  float x = v[abs(s) - 1];
  return s < 0 ? -x : x;
}

// ---- settings ----
void resetSettings() {
  cal.magic = SETTINGS_MAGIC;
  cal.magCal = false;
  for (int i = 0; i < 3; i++) {
    cal.offset[i] = 0;
    cal.scale[i] = 1.0;
  }
  cal.azTrim = 0;
}

void printSettings() {
  if (cal.magCal) {
    Serial.print(F("# cal offset "));
    for (int i = 0; i < 3; i++) { Serial.print(cal.offset[i]); Serial.print(' '); }
    Serial.print(F(" scale "));
    for (int i = 0; i < 3; i++) { Serial.print(cal.scale[i], 3); Serial.print(' '); }
    Serial.println();
  } else {
    Serial.println(F("# magnetometer not calibrated, send 'c' to calibrate"));
  }
  Serial.print(F("# azimuth trim "));
  Serial.println(cal.azTrim, 1);
}

void startCal() {
  for (int i = 0; i < 3; i++) {
    calMin[i] = 32767;
    calMax[i] = -32768;
  }
  calibrating = true;
  Serial.println(F("# calibrating: slowly rotate the board through all orientations, then send 'c'"));
}

void finishCal() {
  calibrating = false;
  float r[3], avg = 0;
  for (int i = 0; i < 3; i++) {
    r[i] = (calMax[i] - (float)calMin[i]) / 2;
    if (r[i] < 300) {
      Serial.println(F("# calibration failed: not enough rotation, try again"));
      return;
    }
    avg += r[i] / 3;
  }
  cal.magCal = true;
  for (int i = 0; i < 3; i++) {
    cal.offset[i] = ((long)calMax[i] + calMin[i]) / 2;  // hard iron
    cal.scale[i] = avg / r[i];                          // simple soft iron
  }
  EEPROM.put(0, cal);
  Serial.println(F("# calibration saved; point forward to a known bearing and send nDEG"));
  printSettings();
}

// ---- main ----
void printHelp() {
  Serial.println(F("# elev_azimuth: c=calibrate nDEG=set bearing r=raw p=print z=erase h=help"));
}

void sample() {
  sensorOk = readAccel(rawAcc) && readMag(rawMag);
  if (!sensorOk) return;

  float a[3], m[3];
  for (int i = 0; i < 3; i++) {
    if (calibrating) {
      calMin[i] = min(calMin[i], rawMag[i]);
      calMax[i] = max(calMax[i], rawMag[i]);
    }
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

// Azimuth without trim
bool computeAngles(Angles &o) {
  // Down, East, North in board frame (accelerometer reads "up" at rest)
  float D[3] = { -acc[0], -acc[1], -acc[2] };
  float E[3], N[3];
  if (!normalize(D)) return false;
  cross(D, mag, E);
  if (!normalize(E)) return false;
  cross(E, D, N);

  // Forward = (1,0,0), so its N/E/D components are the first elements
  o.elev = asin(constrain(-D[0], -1.0, 1.0)) * RAD_TO_DEG;
  o.az = atan2(E[0], N[0]) * RAD_TO_DEG;
  o.roll = atan2(acc[1], acc[2]) * RAD_TO_DEG;
  float b = norm(mag);
  o.b = b / QMC_LSB_PER_UT;
  o.dip = asin(constrain((mag[0] * D[0] + mag[1] * D[1] + mag[2] * D[2]) / b, -1.0, 1.0)) * RAD_TO_DEG;
  return true;
}

void report() {
  if (!sensorOk) {
    Serial.println(F("# I2C read error"));
    return;
  }
  if (rawMode) {
    Serial.print(F("ax:")); Serial.print(rawAcc[0]);
    Serial.print(F(" ay:")); Serial.print(rawAcc[1]);
    Serial.print(F(" az:")); Serial.print(rawAcc[2]);
    Serial.print(F(" mx:")); Serial.print(rawMag[0]);
    Serial.print(F(" my:")); Serial.print(rawMag[1]);
    Serial.print(F(" mz:")); Serial.println(rawMag[2]);
    return;
  }
  if (calibrating) {
    Serial.print(F("# range"));
    for (int i = 0; i < 3; i++) {
      Serial.print(' '); Serial.print(calMin[i]);
      Serial.print(F("..")); Serial.print(calMax[i]);
    }
    Serial.println();
    return;
  }

  Angles o;
  if (!computeAngles(o)) return;
  float az = fmod(o.az + cal.azTrim + 720.0, 360.0);

  Serial.print(F("elev:")); Serial.print(o.elev, 1);
  Serial.print(F(" az:")); Serial.print(az, 1);
  Serial.print(F(" roll:")); Serial.print(o.roll, 1);
  Serial.print(F(" dip:")); Serial.print(o.dip, 1);
  Serial.print(F(" B:")); Serial.println(o.b, 1);
}

void setBearing(float bearing) {
  Angles o;
  if (!sensorOk || !haveSample || !computeAngles(o)) {
    Serial.println(F("# no valid reading"));
    return;
  }
  cal.azTrim = fmod(bearing - o.az + 540.0, 360.0) - 180.0;
  EEPROM.put(0, cal);
  Serial.print(F("# bearing set to "));
  Serial.println(bearing, 1);
  printSettings();
}

void handleSerial() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'c': calibrating ? finishCal() : startCal(); break;
      case 'n': setBearing(Serial.parseFloat(SKIP_WHITESPACE)); break;
      case 'r': rawMode = !rawMode; break;
      case 'p': printSettings(); break;
      case 'z': resetSettings(); EEPROM.put(0, cal); Serial.println(F("# settings erased")); break;
      case 'h': case '?': printHelp(); break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);  // for nDEG
  Wire.begin();
  Wire.setWireTimeout(3000, true);

  writeReg(MPU_ADDR, 0x6B, 0x01);  // wake up, PLL clock
  writeReg(MPU_ADDR, 0x1A, 0x03);  // DLPF ~44 Hz
  writeReg(MPU_ADDR, 0x1C, 0x00);  // accel +-2 g

  writeReg(QMC_ADDR, 0x0A, 0x80);  // soft reset
  delay(10);
  writeReg(QMC_ADDR, 0x0B, 0x01);  // SET/RESET period
  writeReg(QMC_ADDR, 0x09, 0x1D);  // continuous, 200 Hz, +-8 G, OSR 512

  printHelp();
  EEPROM.get(0, cal);
  if (cal.magic != SETTINGS_MAGIC) resetSettings();
  printSettings();
}

void loop() {
  handleSerial();
  unsigned long now = millis();
  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;
    sample();
  }
  if (haveSample && now - lastPrint >= PRINT_MS) {
    lastPrint = now;
    report();
  }
}
