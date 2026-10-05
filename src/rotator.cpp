#include "rotator.h"

#include <ESP8266WiFi.h>

#include "config.h"
#include "net.h"

enum Step : uint8_t { S_POS, S_DIRS, S_FLIP, S_G92, S_MIN, S_MAX, S_SAVE, S_CHECK, STEPS };
static const char *const STEP_NAMES[STEPS] = {
  "reading", "reading", "flipping E axis", "declaring",
  "setting travel", "setting travel", "saving", "checking",
};

static WiFiClient client;
static RotatorState state = ROT_IDLE;
static char message[48];
static char cmds[STEPS][40];
static int8_t step = -1;  // -1 = not connected yet
static unsigned long startedAt, stepAt;
static String line, data;
static float az, declA, declE, prevA, prevE;
static bool prevRef, wantFlipped, flippedE;
static int8_t dirE;  // the rotator's M562 E, from M503; 0 = not seen

static void buildDeclare();

RotatorState rotatorState() { return state; }
const char *rotatorMessage() { return message; }
float rotatorDeclaredA() { return declA; }
float rotatorDeclaredE() { return declE; }
float rotatorPreviousA() { return prevA; }
float rotatorPreviousE() { return prevE; }
bool rotatorWasReferenced() { return prevRef; }
bool rotatorFlippedE() { return flippedE; }

static void fail(const char *why) {
  client.stop();
  state = ROT_FAILED;
  strlcpy(message, why, sizeof message);
  Serial.printf("# rotator: failed, %s\n", why);
}

// One field of an M114 line: "A:12.345 E:45.678 ... Ref:11".
static bool field(const String &s, const char *key, String &out) {
  String k = String(key) + ':';
  int at = s.startsWith(k) ? 0 : s.indexOf(" " + k);
  if (at < 0) return false;
  if (at > 0) at++;
  int end = s.indexOf(' ', at);
  out = s.substring(at + k.length(), end < 0 ? s.length() : end);
  return true;
}

static void send() {
  // One write per command. esp-link classifies a connection by its first
  // packet and "?\n" or "0 " would pulse the AVR's reset; the first command
  // is M114, which is safe.
  char buf[44];
  snprintf(buf, sizeof buf, "%s\n", cmds[step]);
  client.write(buf, strlen(buf));
  strlcpy(message, STEP_NAMES[step], sizeof message);
  stepAt = millis();
  data = "";
  Serial.printf("# rotator: > %s\n", cmds[step]);
}

static void onOk() {
  String v;
  if (step == S_POS) {
    if (!field(data, "A", v)) return fail("no position from M114");
    prevA = v.toFloat();
    prevE = field(data, "E", v) ? v.toFloat() : NAN;
    prevRef = field(data, "Ref", v) && v == "11";
    buildDeclare();
  } else if (step == S_DIRS) {
    if (!dirE) return fail("no M562 line in M503");
    // M564 E flips encoder and motor direction together and mirrors the
    // travel; sent only when the rotator is set for the other side.
    flippedE = (dirE < 0) != wantFlipped;
    if (flippedE) snprintf(cmds[S_FLIP], sizeof cmds[S_FLIP], "M564 E");
    else cmds[S_FLIP][0] = '\0';
  } else if (step == S_CHECK) {
    if (!field(data, "Ref", v) || v != "11") return fail("declared, but Ref is not 11");
    if (!field(data, "A", v) || fabs(v.toFloat() - declA) > 0.1) return fail("declared, but A did not take");
    if (!field(data, "E", v) || fabs(v.toFloat() - declE) > 0.1) return fail("declared, but E did not take");
  }
  do step++; while (step < STEPS && !cmds[step][0]);
  if (step < STEPS) {
    send();
    return;
  }
  client.stop();
  state = ROT_DONE;
  strlcpy(message, "declared", sizeof message);
  Serial.printf("# rotator: declared A%.3f E%.3f, was A%.3f E%.3f%s\n", declA, declE, prevA,
                prevE, prevRef ? "" : " (provisional)");
}

// The rotator counts azimuth continuously, so 339 and -21 are different
// places. Declaring the bearing as 0..360 made a 21-degree-west aim A339, and
// the next "go north" swung the long way round, 339 degrees. Instead take the
// equivalent nearest where the rotator already thinks it is, which keeps the
// count continuous with the cable - provisional 0 at power-up included.
static void buildDeclare() {
  declA = az + 360.0 * roundf((prevA - az) / 360.0);
  snprintf(cmds[S_G92], sizeof cmds[S_G92], "G92 A%.3f E%.3f", declA, declE);
  snprintf(cmds[S_MIN], sizeof cmds[S_MIN], "M208 S1 A%.1f E%.1f", declA - ROTATOR_WRAP_DEG,
           declE - ROTATOR_WRAP_DEG);
  snprintf(cmds[S_MAX], sizeof cmds[S_MAX], "M208 S0 A%.1f E%.1f", declA + ROTATOR_WRAP_DEG,
           declE + ROTATOR_WRAP_DEG);
}

void rotatorDeclare(float bearing, float skyEl, bool flipped) {
  if (state == ROT_BUSY) return;
  if (netState() != NET_ONLINE) {
    fail("wifi is not online");
    return;
  }
  az = fmod(bearing + 360.0, 360.0);
  // Negative E is up on either side: M564 E is what makes that true for the
  // flipped one, so the tracker's el_sign never has to change.
  declE = skyEl * ROTATOR_EL_SIGN;
  wantFlipped = flipped;
  flippedE = false;
  dirE = 0;
  // G92 and M208 are built once M114 says where the rotator thinks it is,
  // and M564 once M503 says which way its E axis is set.
  snprintf(cmds[S_POS], sizeof cmds[S_POS], "M114");
  snprintf(cmds[S_DIRS], sizeof cmds[S_DIRS], "M503");
  snprintf(cmds[S_SAVE], sizeof cmds[S_SAVE], "M500");
  snprintf(cmds[S_CHECK], sizeof cmds[S_CHECK], "M114");
  step = -1;
  line = "";
  startedAt = millis();
  state = ROT_BUSY;
  strlcpy(message, "connecting", sizeof message);
}

void rotatorDismiss() {
  if (state != ROT_BUSY) state = ROT_IDLE;
}

void rotatorUpdate() {
  if (state != ROT_BUSY) return;

  if (step < 0) {
    // connect() blocks, so let one frame show "connecting" first.
    if (millis() - startedAt < DRAW_MS + 50) return;
    if (!client.connect(ROTATOR_HOST, ROTATOR_PORT)) {
      fail("cannot reach " ROTATOR_HOST);
      return;
    }
    client.setNoDelay(true);
    step = 0;
    send();
    return;
  }

  while (client.available()) {
    char ch = client.read();
    if (ch == '\r') continue;
    if (ch != '\n') {
      if (line.length() < 160) line += ch;
      continue;
    }
    String got = line;
    line = "";
    got.trim();
    // M503 reports as notices: "// M562 A-1 E1 / M563 A-1 E1 ...".
    int m562 = got.indexOf("M562");
    if (step == S_DIRS && got.startsWith("//") && m562 >= 0) {
      int e = got.indexOf(" E", m562);
      if (e >= 0) dirE = got.substring(e + 2).toInt() < 0 ? -1 : 1;
      Serial.printf("# rotator: < %s\n", got.c_str());
    }
    // "//" lines are unsolicited notices, and "start" is the boot banner.
    if (!got.length() || got.startsWith("//") || got == "start") continue;
    Serial.printf("# rotator: < %s\n", got.c_str());
    if (got == "ok") {
      onOk();
    } else if (got.startsWith("error:")) {
      got = got.substring(6);
      got.trim();
      fail(got.c_str());
    } else {
      data = got;
    }
    if (state != ROT_BUSY) return;
  }

  if (!client.connected()) {
    fail("rotator hung up");
  } else if (millis() - stepAt > ROTATOR_REPLY_MS) {
    char why[48];
    snprintf(why, sizeof why, "no reply to %s", cmds[step]);
    fail(why);
  }
}
