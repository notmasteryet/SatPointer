#include "ui.h"

#include "calibration.h"
#include "config.h"
#include "input.h"
#include "net.h"
#include "orientation.h"
#include "settings.h"

enum MenuItem : uint8_t {
  MENU_CALIBRATE,
  MENU_MOUNT,
  MENU_WIFI,
  MENU_SCREEN,
  MENU_CLEAR_TARGET,
  MENU_EXIT,
  MENU_COUNT,
};

static const char *const MENU_LABELS[MENU_COUNT] = {
  "Calibrate",
  "Mount",
  "WiFi",
  "Screen",
  "Clear target",
  "Exit",
};

static UiMode mode = UI_MAIN;
static bool editEl = false;
static uint8_t menuIndex = 0;
static uint8_t mountPreview = 0;
static bool graphicScreen = true;

static bool targetDirty = false;
static unsigned long targetTouched = 0;

UiMode uiMode() { return mode; }
bool uiEditEl() { return editEl; }
bool uiGraphicScreen() { return graphicScreen; }
uint8_t uiMenuIndex() { return menuIndex; }
uint8_t uiMenuCount() { return MENU_COUNT; }
const char *uiMenuLabel(uint8_t i) { return MENU_LABELS[i < MENU_COUNT ? i : 0]; }

const char *uiMenuValue(uint8_t i) {
  if (i == MENU_MOUNT) return mountName(cal.mount);
  if (i == MENU_WIFI) return netStateName();
  if (i == MENU_SCREEN) return graphicScreen ? "graphic" : "numbers";
  return nullptr;
}
uint8_t uiMountPreview() { return mountPreview; }

// The target is written to EEPROM only once the dial has been still for a
// while - otherwise a single sweep of the encoder would burn a flash write per
// detent.
static void touchTarget() {
  targetDirty = true;
  targetTouched = millis();
}

static void flushTarget() {
  if (!targetDirty || millis() - targetTouched < TARGET_SAVE_MS) return;
  targetDirty = false;
  settingsSave();
  Serial.printf("# target saved az %.1f el %.1f\n", cal.targetAz, cal.targetEl);
}

void uiSetMount(uint8_t m) {
  if (m == cal.mount) return;
  cal.mount = m;
  settingsSave();
  // The filter holds vectors in the old frame; blending them across a mount
  // change would drift through nonsense for a second or so.
  orientationReset();
  Serial.printf("# mount %s (beam %s) - recalibrate, the beam's metal moved\n",
                mountName(cal.mount), mountBeamAxis(cal.mount));
}

static void runMenuItem() {
  switch (menuIndex) {
    case MENU_CALIBRATE:
      calStart();
      mode = UI_MAIN;
      break;
    case MENU_MOUNT:
      mountPreview = cal.mount;
      mode = UI_MOUNT;
      break;
    case MENU_WIFI:
      mode = UI_WIFI;
      break;
    case MENU_SCREEN:
      graphicScreen = !graphicScreen;
      break;
    case MENU_CLEAR_TARGET:
      cal.targetAz = 0;
      cal.targetEl = 0;
      touchTarget();
      Serial.println(F("# target cleared"));
      mode = UI_MAIN;
      break;
    case MENU_EXIT:
    default:
      mode = UI_MAIN;
      break;
  }
}

static void wrap(uint8_t &v, int8_t delta, uint8_t count) {
  int16_t n = (int16_t)v + delta;
  while (n < 0) n += count;
  v = n % count;
}

void uiUpdate() {
  inputPoll();
  int8_t detents = inputTakeDetents();
  InputEvent ev = inputTakeEvent();

  // Calibration takes over whichever way it was started.
  if (calActive()) {
    if (ev == InputEvent::ShortPress) calFinish();
    else if (ev == InputEvent::LongPress) calCancel();
    flushTarget();
    return;
  }

  // The verdict owns the screen while it is up, so a press acknowledges it
  // instead of doing whatever that press would otherwise have done.
  if (calResultFresh() && ev != InputEvent::None) {
    calResultDismiss();
    flushTarget();
    return;
  }

  switch (mode) {
    case UI_MAIN:
      if (detents) {
        if (editEl) {
          cal.targetEl = constrain(cal.targetEl + detents * STEP_DEG, -90.0, 90.0);
        } else {
          cal.targetAz = fmod(cal.targetAz + detents * STEP_DEG + 360.0, 360.0);
        }
        touchTarget();
      }
      if (ev == InputEvent::ShortPress) {
        editEl = !editEl;
        Serial.printf("# editing %s\n", editEl ? "elevation" : "azimuth");
      } else if (ev == InputEvent::LongPress) {
        menuIndex = 0;
        mode = UI_MENU;
      }
      break;

    case UI_MENU:
      if (detents) wrap(menuIndex, detents, MENU_COUNT);
      if (ev == InputEvent::ShortPress) runMenuItem();
      else if (ev == InputEvent::LongPress) mode = UI_MAIN;
      break;

    case UI_MOUNT:
      if (detents) wrap(mountPreview, detents, mountCount());
      if (ev == InputEvent::ShortPress) {
        uiSetMount(mountPreview);
        mode = UI_MENU;
      } else if (ev == InputEvent::LongPress) {
        mode = UI_MENU;  // cancelled, nothing applied
      }
      break;

    case UI_WIFI:
      if (ev == InputEvent::ShortPress) netSetEnabled(!cal.wifiOn);
      else if (ev == InputEvent::LongPress) mode = UI_MENU;
      break;
  }

  flushTarget();
}
