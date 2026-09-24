// SatPointer - elevation and azimuth readout for aiming a satellite dish.
// ESP-12F + GY-521 (MPU6050 @0x68) + GY-271 (QMC5883L @0x0D) + SSD1306 @0x3C.
//
// Wiring: all three modules share one I2C bus.
//   VCC -> 3.3V   GND -> GND   SDA -> GPIO4   SCL -> GPIO5
//   The ESP8266 is NOT 5V tolerant - do not power the modules from 5V.
//
// Board frame: X = forward = GY-521 +X arrow (= GY-271 +Y arrow),
//              Y = left = GY-521 +Y arrow, Z = up.
//   elev = angle of forward above the horizon (-90..+90)
//   az   = true bearing of forward (0..360, 0 = north, 90 = east, clockwise)
//   roll = rotation around forward, right side down positive
//   dip  = magnetic inclination; should stay constant however you turn the board
//   B    = field strength in uT; Earth is 25..65, much more means interference
//
// Serial 115200, commands:
//   c  start / finish magnetometer calibration (slowly rotate board all ways)
//   nDEG  forward currently points to true bearing DEG (e.g. n90 = east)
//   r  toggle raw sensor output
//   e  print encoder pin states (wiring check)
//   p  print stored settings
//   z  erase settings
//   h  help

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>

// ---- configuration ----
// Sensor axes expressed in the board frame (forward, left, up):
// +1/-1 = sensor X, +2/-2 = sensor Y, +3/-3 = sensor Z.
const int8_t ACC_AXIS[3] = { +1, +2, +3 };  // GY-521: X forward, Y left
const int8_t MAG_AXIS[3] = { +2, -1, +3 };  // GY-271: Y forward, X right

const uint8_t MPU_ADDR = 0x68;
const uint8_t QMC_ADDR = 0x0D;
const uint8_t OLED_ADDR = 0x3C;
const uint8_t PIN_SDA = 4;
const uint8_t PIN_SCL = 5;

// Rotary encoder (EC11-style, 4 quadrature counts per detent).
// The button sits on GPIO0, which is also the boot strapping pin: holding it
// down through a reset enters the flash bootloader instead of the app. That is
// useful rather than harmful - it gives a real flash button - but it does mean
// the pin must idle high, hence INPUT_PULLUP.
const uint8_t PIN_ENC_A = 13;
const uint8_t PIN_ENC_B = 14;
const uint8_t PIN_ENC_BTN = 0;
const int8_t COUNTS_PER_DETENT = 4;
const float STEP_DEG = 1.0;
const unsigned long DEBOUNCE_MS = 30;
const unsigned long LONGPRESS_MS = 800;

const float ALPHA = 0.2;             // low-pass factor per sample, lower = smoother
const unsigned long SAMPLE_MS = 20;  // 50 Hz
const unsigned long PRINT_MS = 100;  // 10 Hz
const unsigned long DRAW_MS = 200;   // 5 Hz, a full SSD1306 frame is not cheap
const float QMC_LSB_PER_UT = 30.0;   // +-8 G range: 3000 LSB/G

// ESP8266 EEPROM is a RAM shadow of a flash sector: it needs begin() and
// commit(), unlike the AVR version this started life on.
const size_t EEPROM_SIZE = 128;

struct Settings {
  uint16_t magic;
  bool magCal;
  int16_t offset[3];
  float scale[3];
  float azTrim;     // declination + mounting error, set with 'n'
  float targetAz;   // what we are aiming at, set with the encoder
  float targetEl;
};
// Bumped when targetAz/targetEl were added, so an older layout in flash is
// discarded rather than read as garbage.
const uint16_t SETTINGS_MAGIC = 0xCA1D;
Settings cal;

struct Angles {
  float elev, az, roll, dip, b;
};

static Adafruit_SSD1306 display(128, OLED_H, &Wire, -1);

int16_t rawAcc[3], rawMag[3];
float acc[3], mag[3];  // filtered, board frame
bool haveSample = false, sensorOk = false, rawMode = false, calibrating = false;
bool haveDisplay = false;
int16_t calMin[3], calMax[3];
unsigned long lastSample, lastPrint, lastDraw;

// ---- encoder ----
bool editEl = false;          // false = the dial edits azimuth
bool targetDirty = false;     // target moved, not yet written to EEPROM
unsigned long targetTouched;  // when it last moved

volatile int8_t encCount = 0;
volatile uint8_t encState = 0;

// Quadrature state table: index is (previous << 2 | current), value is the
// direction of travel. Invalid transitions (a missed edge) map to 0.
const int8_t ENC_TABLE[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };

void IRAM_ATTR encISR() {
  uint8_t s = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  encCount += ENC_TABLE[(encState << 2) | s];
  encState = s;
}

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

bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
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
  cal.targetAz = 0;
  cal.targetEl = 0;
}

void saveSettings() {
  EEPROM.put(0, cal);
  if (!EEPROM.commit()) Serial.println(F("# EEPROM commit failed"));
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
  Serial.print(F("# target az "));
  Serial.print(cal.targetAz, 1);
  Serial.print(F(" el "));
  Serial.println(cal.targetEl, 1);
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
  saveSettings();
  Serial.println(F("# calibration saved; point forward to a known bearing and send nDEG"));
  printSettings();
}

// ---- main ----
void printHelp() {
  Serial.println(F("# SatPointer: c=calibrate nDEG=set bearing r=raw e=encoder p=print z=erase h=help"));
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

float trimmedAz(const Angles &o) {
  return fmod(o.az + cal.azTrim + 720.0, 360.0);
}

// Shortest signed way round: positive means turn clockwise (right).
float azError(float current) {
  return fmod(cal.targetAz - current + 540.0, 360.0) - 180.0;
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

  float az = trimmedAz(o);
  Serial.print(F("elev:")); Serial.print(o.elev, 1);
  Serial.print(F(" az:")); Serial.print(az, 1);
  Serial.print(F(" daz:")); Serial.print(azError(az), 1);
  Serial.print(F(" del:")); Serial.print(cal.targetEl - o.elev, 1);
  Serial.print(F(" roll:")); Serial.print(o.roll, 1);
  Serial.print(F(" dip:")); Serial.print(o.dip, 1);
  Serial.print(F(" B:")); Serial.println(o.b, 1);
}

// ---- display ----
// Calibration shows per-axis swing, because "not enough rotation" is the usual
// failure and it is much easier to fix while you can watch the numbers grow.
void drawCalibrating() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("CALIBRATING"));
  display.println(F("rotate all ways"));
  for (int i = 0; i < 3; i++) {
    long span = (long)calMax[i] - calMin[i];
    display.printf("%c %6ld ", 'X' + i, span);
    int16_t w = constrain(span / 40, 0, 60);
    display.drawRect(66, 22 + i * 10, 62, 7, SSD1306_WHITE);
    display.fillRect(66, 22 + i * 10, w, 7, SSD1306_WHITE);
    display.println();
  }
  display.setCursor(0, 56);
  display.print(F("send 'c' to finish"));
}

void drawAngles() {
  Angles o;
  if (!computeAngles(o)) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("no fix"));
    return;
  }

  float az = trimmedAz(o);
  float dAz = azError(az);
  float dEl = cal.targetEl - o.elev;

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("AZ%6.1f ->%6.1f", az, cal.targetAz);
  display.setCursor(0, 10);
  display.printf("EL%6.1f ->%6.1f", o.elev, cal.targetEl);
  display.drawFastHLine(0, 20, 128, SSD1306_WHITE);

  // The whole point of the tool: which way to turn, and how far. Show the word
  // before the number so it reads at a glance while you are moving the dish.
  display.setTextSize(1);
  if (fabs(dAz) < 0.5) {
    display.setCursor(0, 26);
    display.print(F("az  ON TARGET"));
  } else {
    display.setCursor(0, 26);
    display.printf("turn %-5s %5.1f", dAz > 0 ? "RIGHT" : "LEFT", fabs(dAz));
  }
  if (fabs(dEl) < 0.5) {
    display.setCursor(0, 38);
    display.print(F("el  ON TARGET"));
  } else {
    display.setCursor(0, 38);
    display.printf("%-10s %5.1f", dEl > 0 ? "raise" : "lower", fabs(dEl));
  }

  // Which axis the dial edits, and whether azimuth can be trusted at all.
  display.setCursor(0, 54);
  if (editEl) {
    display.print(F("az  [EL]"));
  } else {
    display.print(F("[AZ]  el"));
  }
  display.setCursor(74, 54);
  display.print(cal.magCal ? F("cal ok") : F("NO CAL"));
}

void draw() {
  if (!haveDisplay) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (!sensorOk) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("I2C read error"));
    display.println();
    display.printf("mpu6050 %s\n", probe(MPU_ADDR) ? "ok" : "MISSING");
    display.printf("qmc5883 %s\n", probe(QMC_ADDR) ? "ok" : "MISSING");
  } else if (calibrating) {
    drawCalibrating();
  } else {
    drawAngles();
  }
  display.display();
}

void setBearing(float bearing) {
  Angles o;
  if (!sensorOk || !haveSample || !computeAngles(o)) {
    Serial.println(F("# no valid reading"));
    return;
  }
  cal.azTrim = fmod(bearing - o.az + 540.0, 360.0) - 180.0;
  saveSettings();
  Serial.print(F("# bearing set to "));
  Serial.println(bearing, 1);
  printSettings();
}

// ---- target / encoder ----
// The target is written to EEPROM only once the dial has been still for a
// while - otherwise a single sweep of the encoder would burn a flash write per
// detent.
void touchTarget() {
  targetDirty = true;
  targetTouched = millis();
}

void handleEncoder() {
  noInterrupts();
  int8_t c = encCount;
  encCount = 0;
  interrupts();

  if (c) {
    static int8_t acc = 0;
    acc += c;
    int8_t detents = acc / COUNTS_PER_DETENT;
    acc -= detents * COUNTS_PER_DETENT;
    if (detents) {
      if (editEl) {
        cal.targetEl = constrain(cal.targetEl + detents * STEP_DEG, -90.0, 90.0);
      } else {
        cal.targetAz = fmod(cal.targetAz + detents * STEP_DEG + 360.0, 360.0);
      }
      touchTarget();
    }
  }

  // Button: short press swaps the axis being edited, long press clears both.
  static bool pressed = false;
  static bool longFired = false;
  static unsigned long edge = 0;
  bool down = digitalRead(PIN_ENC_BTN) == LOW;
  unsigned long now = millis();

  if (down != pressed && now - edge > DEBOUNCE_MS) {
    edge = now;
    pressed = down;
    if (down) {
      longFired = false;
    } else if (!longFired) {
      editEl = !editEl;
      Serial.printf("# editing %s\n", editEl ? "elevation" : "azimuth");
    }
  }
  if (pressed && !longFired && now - edge >= LONGPRESS_MS) {
    longFired = true;
    cal.targetAz = 0;
    cal.targetEl = 0;
    touchTarget();
    Serial.println(F("# target cleared"));
  }

  if (targetDirty && now - targetTouched > 2000) {
    targetDirty = false;
    saveSettings();
    Serial.printf("# target saved az %.1f el %.1f\n", cal.targetAz, cal.targetEl);
  }
}

void handleSerial() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'c': calibrating ? finishCal() : startCal(); break;
      // The ESP8266 core's Stream has no LookaheadMode overload; the plain
      // parseFloat() already skips leading non-numeric characters.
      case 'n': setBearing(Serial.parseFloat()); break;
      case 'r': rawMode = !rawMode; break;
      case 'p': printSettings(); break;
      case 'z': resetSettings(); saveSettings(); Serial.println(F("# settings erased")); break;
      // Separates "encoder not wired" from "decoder logic wrong": if A/B never
      // change as you turn, the quadrature common pin is not grounded.
      case 'e': {
        noInterrupts();
        int8_t c = encCount;
        interrupts();
        Serial.printf("# enc A=%d B=%d btn=%d count=%d edits=%s\n",
                      digitalRead(PIN_ENC_A), digitalRead(PIN_ENC_B),
                      digitalRead(PIN_ENC_BTN), c, editEl ? "el" : "az");
        break;
      }
      case 'h': case '?': printHelp(); break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);  // for nDEG
  delay(200);
  Serial.println();

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_BTN, INPUT_PULLUP);
  encState = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  // The MPU6050 stretches the clock; the ESP8266 default limit is too short.
  Wire.setClockStretchLimit(1500);

  Serial.println(F("# SatPointer starting"));
  Serial.printf("# mpu6050@0x%02X %s\n", MPU_ADDR, probe(MPU_ADDR) ? "found" : "MISSING");
  Serial.printf("# qmc5883@0x%02X %s\n", QMC_ADDR, probe(QMC_ADDR) ? "found" : "MISSING");

  haveDisplay = probe(OLED_ADDR) && display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  Serial.printf("# ssd1306@0x%02X %s\n", OLED_ADDR, haveDisplay ? "found" : "MISSING");
  if (haveDisplay) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("SatPointer"));
    display.display();
  }

  writeReg(MPU_ADDR, 0x6B, 0x01);  // wake up, PLL clock
  writeReg(MPU_ADDR, 0x1A, 0x03);  // DLPF ~44 Hz
  writeReg(MPU_ADDR, 0x1C, 0x00);  // accel +-2 g

  writeReg(QMC_ADDR, 0x0A, 0x80);  // soft reset
  delay(10);
  writeReg(QMC_ADDR, 0x0B, 0x01);  // SET/RESET period
  writeReg(QMC_ADDR, 0x09, 0x1D);  // continuous, 200 Hz, +-8 G, OSR 512

  printHelp();
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, cal);
  if (cal.magic != SETTINGS_MAGIC) resetSettings();
  printSettings();
}

void loop() {
  handleSerial();
  handleEncoder();
  unsigned long now = millis();
  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;
    sample();
  }
  if (haveSample && now - lastPrint >= PRINT_MS) {
    lastPrint = now;
    report();
  }
  if (haveSample && now - lastDraw >= DRAW_MS) {
    lastDraw = now;
    draw();
  }
  yield();
}
