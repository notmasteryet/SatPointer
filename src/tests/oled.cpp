#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

// Wiring: SDA -> GPIO4 (D2), SCL -> GPIO5 (D1), VCC -> 3.3V, GND -> GND.
static const uint8_t PIN_SDA = 4;
static const uint8_t PIN_SCL = 5;

// OLED_H is set in platformio.ini (64 for a 0.96" panel, 32 for a 0.91").
static const uint8_t W = 128;
static const uint8_t H = OLED_H;

static Adafruit_SSD1306 display(W, H, &Wire, -1);
static const uint8_t LED = LED_BUILTIN;

static uint8_t addr = 0;
static uint32_t frame = 0;

// Probe the bus and report every device, so a wiring fault is distinguishable
// from a wrong address.
static uint8_t scanI2C() {
  uint8_t found = 0;
  Serial.println(F("scanning I2C..."));
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device at 0x%02X\n", a);
      if ((a == 0x3C || a == 0x3D) && !found) found = a;
    }
  }
  if (!found) Serial.println(F("  none found"));
  return found;
}

void setup() {
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("=== SSD1306 OLED test ==="));
  Serial.printf("i2c: sda=GPIO%u scl=GPIO%u, panel %ux%u\n", PIN_SDA, PIN_SCL, W, H);

  Wire.begin(PIN_SDA, PIN_SCL);
  addr = scanI2C();

  if (!addr) {
    Serial.println(F("FAIL: no SSD1306 on the bus. Check SDA/SCL, 3.3V, GND,"));
    Serial.println(F("      and that pull-ups are present (most modules have them)."));
    return;
  }

  Serial.printf("using 0x%02X\n", addr);
  if (!display.begin(SSD1306_SWITCHCAPVCC, addr)) {
    Serial.println(F("FAIL: display.begin() rejected the panel"));
    addr = 0;
    return;
  }

  Serial.println(F("OK: display initialised"));

  // Full-screen fill first: every pixel lit shows dead rows/columns at a glance.
  display.clearDisplay();
  display.fillRect(0, 0, W, H, SSD1306_WHITE);
  display.display();
  delay(600);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("SSD1306 OK"));
  display.printf("addr 0x%02X\n", addr);
  display.printf("%ux%u\n", W, H);
  display.printf("chip %08X\n", ESP.getChipId());
  display.display();
  delay(1500);

  Serial.println(F("running animation..."));
}

void loop() {
  if (!addr) {  // nothing on the bus - blink fast so the failure is visible
    digitalWrite(LED, LOW);
    delay(100);
    digitalWrite(LED, HIGH);
    delay(100);
    return;
  }

  // Sweeping box + counter: a frozen frame means the panel stopped taking data.
  display.clearDisplay();
  display.drawRect(0, 0, W, H, SSD1306_WHITE);

  int16_t x = (frame * 3) % (W - 16);
  display.fillRect(x, H / 2 - 4, 8, 8, SSD1306_WHITE);

  display.setCursor(4, 4);
  display.printf("frame %lu", frame);
  display.setCursor(4, H - 12);
  display.printf("up %lus heap %u", millis() / 1000, ESP.getFreeHeap());
  display.display();

  digitalWrite(LED, (frame % 10 < 5) ? LOW : HIGH);

  if (++frame % 50 == 0) {
    Serial.printf("frame %lu  uptime %lus  heap %u\n", frame, millis() / 1000,
                  ESP.getFreeHeap());
  }
  delay(20);
}
