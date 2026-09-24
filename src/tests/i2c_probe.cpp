// I2C scanner + chip ID probe for the SatPointer bus (GY-521 / GY-271 / SSD1306)
#include <Arduino.h>
#include <Wire.h>

static const uint8_t PIN_SDA = 4;
static const uint8_t PIN_SCL = 5;

int readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

void hex2(int v) {
  if (v < 0) { Serial.print(F("--")); return; }
  if (v < 16) Serial.print('0');
  Serial.print(v, HEX);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClockStretchLimit(1500);
}

void loop() {
  Serial.println(F("=== I2C scan ==="));
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() != 0) continue;
    found++;
    Serial.print(F("0x")); hex2(a); Serial.print(F("  "));
    if (a == 0x68 || a == 0x69) {
      Serial.print(F("MPU6050? WHO_AM_I(0x75)=0x")); hex2(readReg(a, 0x75));
    } else if (a == 0x1E) {
      Serial.print(F("HMC5883L? ID(0x0A..0C)="));
      Serial.write(readReg(a, 0x0A)); Serial.write(readReg(a, 0x0B)); Serial.write(readReg(a, 0x0C));
    } else if (a == 0x0D) {
      Serial.print(F("QMC5883L? CHIPID(0x0D)=0x")); hex2(readReg(a, 0x0D));
    } else if (a == 0x2C) {
      Serial.print(F("QMC5883P? CHIPID(0x00)=0x")); hex2(readReg(a, 0x00));
    } else if (a == 0x3C || a == 0x3D) {
      Serial.print(F("SSD1306 OLED (write-only, no ID register)"));
    } else {
      Serial.print(F("unknown, reg0=0x")); hex2(readReg(a, 0x00));
    }
    Serial.println();
  }
  Serial.print(F("devices: ")); Serial.println(found);
  delay(3000);
}
