#include "input.h"

#include "config.h"
#include "settings.h"

// Quadrature state table: index is (previous << 2 | current), value is the
// direction of travel. Invalid transitions (a missed edge) map to 0.
static const int8_t ENC_TABLE[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };

static volatile int8_t encCount = 0;
static volatile uint8_t encState = 0;

static bool editEl = false;
static bool targetDirty = false;
static unsigned long targetTouched = 0;

bool inputEditEl() { return editEl; }

static void IRAM_ATTR encISR() {
  uint8_t s = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  encCount += ENC_TABLE[(encState << 2) | s];
  encState = s;
}

void inputBegin() {
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_BTN, INPUT_PULLUP);
  encState = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);
}

// The target is written to EEPROM only once the dial has been still for a
// while - otherwise a single sweep of the encoder would burn a flash write per
// detent.
static void touchTarget() {
  targetDirty = true;
  targetTouched = millis();
}

static void handleRotation() {
  noInterrupts();
  int8_t c = encCount;
  encCount = 0;
  interrupts();
  if (!c) return;

  static int8_t partial = 0;
  partial += c;
  int8_t detents = partial / COUNTS_PER_DETENT;
  partial -= detents * COUNTS_PER_DETENT;
  if (!detents) return;

  if (editEl) {
    cal.targetEl = constrain(cal.targetEl + detents * STEP_DEG, -90.0, 90.0);
  } else {
    cal.targetAz = fmod(cal.targetAz + detents * STEP_DEG + 360.0, 360.0);
  }
  touchTarget();
}

// Short press swaps the axis being edited, long press clears both targets.
static void handleButton() {
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
}

void inputUpdate() {
  handleRotation();
  handleButton();

  if (targetDirty && millis() - targetTouched > TARGET_SAVE_MS) {
    targetDirty = false;
    settingsSave();
    Serial.printf("# target saved az %.1f el %.1f\n", cal.targetAz, cal.targetEl);
  }
}

// Separates "encoder not wired" from "decoder logic wrong": if A/B never
// change as you turn, the quadrature common pin is not grounded.
void inputDiag() {
  Serial.printf("# enc A=%d B=%d btn=%d edits=%s\n", digitalRead(PIN_ENC_A),
                digitalRead(PIN_ENC_B), digitalRead(PIN_ENC_BTN),
                editEl ? "el" : "az");
}
