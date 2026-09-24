// Interaction policy: what the dial and the button do in each mode.
//
//   MAIN  turn = move target, press = swap az/el, hold = open menu
//   MENU  turn = choose, press = activate, hold = back
//   MOUNT turn = preview a mount, press = apply, hold = cancel
//
// Calibration is not a mode here: it is whatever calActive() says, so the
// serial 'c' command and the menu entry lead to the same place.
#pragma once

#include <Arduino.h>

enum UiMode : uint8_t { UI_MAIN, UI_MENU, UI_MOUNT };

void uiUpdate();

UiMode uiMode();
bool uiEditEl();          // in MAIN, true when the dial edits elevation
uint8_t uiMenuIndex();
uint8_t uiMenuCount();
const char *uiMenuLabel(uint8_t i);
const char *uiMenuValue(uint8_t i);  // value shown beside the item, or nullptr
uint8_t uiMountPreview();  // mount highlighted in UI_MOUNT, not yet applied

// Applies a mount and does the bookkeeping that must go with it. Shared with
// the serial 'm' command so there is only one place that gets it right.
void uiSetMount(uint8_t m);
