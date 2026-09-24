#include "sensors.h"

#include <Wire.h>

#include "config.h"

static void writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static bool readBytes(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static int readReg(uint8_t addr, uint8_t reg) {
  uint8_t v;
  return readBytes(addr, reg, &v, 1) ? v : -1;
}

bool sensorsProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void sensorsBegin() {
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  // The MPU6050 stretches the clock; the ESP8266 default limit is too short.
  Wire.setClockStretchLimit(1500);

  writeReg(MPU_ADDR, 0x6B, 0x01);  // wake up, PLL clock
  writeReg(MPU_ADDR, 0x1A, 0x03);  // DLPF ~44 Hz
  writeReg(MPU_ADDR, 0x1C, 0x00);  // accel +-2 g

  writeReg(QMC_ADDR, 0x0A, 0x80);  // soft reset
  delay(10);
  writeReg(QMC_ADDR, 0x0B, 0x01);  // SET/RESET period
  writeReg(QMC_ADDR, 0x09, 0x1D);  // continuous, 200 Hz, +-8 G, OSR 512
}

void sensorsScanReport() {
  Serial.println(F("# i2c scan"));
  uint8_t found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    if (!sensorsProbe(a)) continue;
    found++;
    if (a == MPU_ADDR) {
      Serial.printf("#   0x%02X mpu6050, WHO_AM_I=0x%02X\n", a, readReg(a, 0x75));
    } else if (a == QMC_ADDR) {
      Serial.printf("#   0x%02X qmc5883l, CHIPID=0x%02X\n", a, readReg(a, 0x0D));
    } else if (a == OLED_ADDR || a == 0x3D) {
      Serial.printf("#   0x%02X ssd1306\n", a);
    } else if (a == 0x1E) {
      Serial.printf("#   0x%02X HMC5883L - unsupported magnetometer variant\n", a);
    } else if (a == 0x2C) {
      Serial.printf("#   0x%02X QMC5883P - unsupported magnetometer variant\n", a);
    } else {
      Serial.printf("#   0x%02X unknown, reg0=0x%02X\n", a, readReg(a, 0x00));
    }
  }
  if (!found) Serial.println(F("#   nothing on the bus"));
}

static bool readAccel(int16_t a[3]) {
  uint8_t b[6];
  if (!readBytes(MPU_ADDR, 0x3B, b, 6)) return false;  // X,Y,Z big-endian
  for (int i = 0; i < 3; i++) a[i] = (int16_t)((uint16_t)b[2 * i] << 8 | b[2 * i + 1]);
  return true;
}

static bool readMag(int16_t m[3]) {
  uint8_t b[7];
  if (!readBytes(QMC_ADDR, 0x00, b, 7)) return false;  // X,Y,Z little-endian + status
  if (b[6] & 0x02) return false;                        // overflow
  for (int i = 0; i < 3; i++) m[i] = (int16_t)((uint16_t)b[2 * i + 1] << 8 | b[2 * i]);
  return true;
}

bool sensorsRead(int16_t acc[3], int16_t mag[3]) {
  return readAccel(acc) && readMag(mag);
}
