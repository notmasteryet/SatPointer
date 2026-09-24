#include "input.h"

#include "config.h"

// Quadrature state table: index is (previous << 2 | current), value is the
// direction of travel. Invalid transitions (a missed edge) map to 0.
static const int8_t ENC_TABLE[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };

static volatile int8_t encCount = 0;
static volatile uint8_t encState = 0;

static int8_t partial = 0;   // quadrature counts not yet a whole detent
static int8_t detents = 0;   // whole detents waiting to be read
static InputEvent pending = InputEvent::None;

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

// The long press has to fire while the button is still held, not on release -
// otherwise you cannot tell you have held it long enough without letting go.
void inputPoll() {
  noInterrupts();
  int8_t c = encCount;
  encCount = 0;
  interrupts();
  if (c) {
    partial += c;
    int8_t whole = partial / COUNTS_PER_DETENT;
    partial -= whole * COUNTS_PER_DETENT;
    detents += whole;
  }

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
      pending = InputEvent::ShortPress;
    }
  }
  if (pressed && !longFired && now - edge >= LONGPRESS_MS) {
    longFired = true;
    pending = InputEvent::LongPress;
  }
}

int8_t inputTakeDetents() {
  int8_t d = detents;
  detents = 0;
  return d;
}

InputEvent inputTakeEvent() {
  InputEvent e = pending;
  pending = InputEvent::None;
  return e;
}

// Separates "encoder not wired" from "decoder logic wrong": if A/B never
// change as you turn, the quadrature common pin is not grounded.
void inputDiag() {
  Serial.printf("# enc A=%d B=%d btn=%d\n", digitalRead(PIN_ENC_A),
                digitalRead(PIN_ENC_B), digitalRead(PIN_ENC_BTN));
}
