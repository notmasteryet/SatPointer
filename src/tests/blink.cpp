#include <Arduino.h>

// The onboard LED on most ESP8266 boards is GPIO2 and is active LOW.
static const uint8_t LED = LED_BUILTIN;
static const uint32_t PERIOD_MS = 500;

static uint32_t ticks = 0;

void setup() {
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);  // off

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("=== ESP8266 blink test ==="));
  Serial.printf("chip id:    %08X\n", ESP.getChipId());
  Serial.printf("flash id:   %08X\n", ESP.getFlashChipId());
  Serial.printf("flash size: %u bytes (real %u)\n", ESP.getFlashChipSize(),
                ESP.getFlashChipRealSize());
  Serial.printf("cpu freq:   %u MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("free heap:  %u bytes\n", ESP.getFreeHeap());
  Serial.printf("sdk:        %s\n", ESP.getSdkVersion());
  Serial.printf("reset:      %s\n", ESP.getResetReason().c_str());
  Serial.println(F("blinking LED_BUILTIN..."));
}

void loop() {
  digitalWrite(LED, LOW);  // on
  delay(PERIOD_MS);
  digitalWrite(LED, HIGH);  // off
  delay(PERIOD_MS);

  Serial.printf("tick %lu  uptime %lus  heap %u\n", ++ticks, millis() / 1000,
                ESP.getFreeHeap());
}
