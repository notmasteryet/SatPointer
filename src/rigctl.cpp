#include "rigctl.h"

#include "lineserver.h"

static void handleLine(String &cmd);
static void reset();
static LineServer srv(4532, "rigctl", handleLine, reset);

// Index 0 is VFOA / Main, 1 is VFOB / Sub.
static double vfoHz[2];
static uint8_t vfo = 0;
static double splitHz = 0;
static bool split = false;
static char mode[12] = "FM";
static long passband = 15000;

bool rigctlConnected() { return srv.connected(); }
double rigctlFreq() { return srv.connected() ? vfoHz[0] : 0; }

// A new session starts from nothing, so a frequency from the last pass is not
// shown as though it were current.
static void reset() {
  vfoHz[0] = vfoHz[1] = splitHz = 0;
  vfo = 0;
  split = false;
}

static int vfoIndex(const String &name) {
  if (name == "VFOA" || name == "Main" || name == "currVFO") return 0;
  if (name == "VFOB" || name == "Sub") return 1;
  return -1;
}

static const char *vfoName(uint8_t i) { return i ? "VFOB" : "VFOA"; }

// Long names for the short commands below. Matched whole, because several
// share a prefix (set_split_freq / set_split_vfo / set_freq). get_dcd and
// chk_vfo have no letter; 0x8b and 0xf0 are the codes hamlib uses for them.
static const struct { const char *name; char c; } LONG_NAMES[] = {
  {"set_freq", 'F'}, {"get_freq", 'f'},
  {"set_split_freq", 'I'}, {"get_split_freq", 'i'},
  {"set_vfo", 'V'}, {"get_vfo", 'v'},
  {"set_split_vfo", 'S'}, {"get_split_vfo", 's'},
  {"set_mode", 'M'}, {"get_mode", 'm'},
  {"set_ptt", 'T'}, {"get_ptt", 't'},
  {"get_dcd", '\x8b'},
  {"chk_vfo", '\xf0'},
};

static void sendHz(double hz) {
  char r[20];
  snprintf(r, sizeof r, "%.0f\n", hz);
  srv.reply(r);
}

static void handleLine(String &cmd) {
  String word = cmd, arg;
  int sp = cmd.indexOf(' ');
  if (sp >= 0) {
    word = cmd.substring(0, sp);
    arg = cmd.substring(sp + 1);
    arg.trim();
  }

  char c = word[0];
  if (word.startsWith("\\")) {
    String name = word.substring(1);
    c = 0;
    for (auto &n : LONG_NAMES)
      if (name == n.name) c = n.c;
  } else if (word.length() != 1) {
    c = 0;
  }

  // strtod rather than toFloat: a float has 24 bits of mantissa, which at
  // 435 MHz rounds to the nearest 32 Hz - coarser than the Doppler steps.
  switch (c) {
    case 'F':
      vfoHz[vfo] = strtod(arg.c_str(), nullptr);
      srv.reply("RPRT 0\n");
      return;
    case 'f':
      sendHz(vfoHz[vfo]);
      return;

    case 'I':
      splitHz = strtod(arg.c_str(), nullptr);
      srv.reply("RPRT 0\n");
      return;
    case 'i':
      sendHz(splitHz);
      return;

    case 'V': {
      int i = vfoIndex(arg);
      if (i < 0) break;
      vfo = i;
      srv.reply("RPRT 0\n");
      return;
    }
    case 'v': {
      char r[8];
      snprintf(r, sizeof r, "%s\n", vfoName(vfo));
      srv.reply(r);
      return;
    }

    case 'S':  // "S 1 VFOB": split on, transmit on VFOB
      split = arg.toInt() != 0;
      srv.reply("RPRT 0\n");
      return;
    case 's': {
      char r[12];
      snprintf(r, sizeof r, "%d\n%s\n", split ? 1 : 0, vfoName(1));
      srv.reply(r);
      return;
    }

    case 'M': {  // "M FM 15000"
      int sp2 = arg.indexOf(' ');
      String m = sp2 < 0 ? arg : arg.substring(0, sp2);
      strncpy(mode, m.c_str(), sizeof mode - 1);
      mode[sizeof mode - 1] = '\0';
      if (sp2 >= 0) passband = arg.substring(sp2 + 1).toInt();
      srv.reply("RPRT 0\n");
      return;
    }
    case 'm': {
      char r[28];
      snprintf(r, sizeof r, "%s\n%ld\n", mode, passband);
      srv.reply(r);
      return;
    }

    // Nothing transmits and nothing receives: report the radio idle.
    case 'T':
      srv.reply("RPRT 0\n");
      return;
    case 't':
    case '\x8b':
      srv.reply("0\n");
      return;

    case '\xf0':  // not running rigctld --vfo
      srv.reply("0\n");
      return;

    case 'q':
    case 'Q':
      srv.close();
      return;

    case '_':
      srv.reply("SatPointer\n");
      return;
  }

  // Logged rather than silently refused: the first real gpredict session
  // is what tells us whether anything else is needed.
  Serial.printf("# rigctl: unhandled '%s'\n", cmd.c_str());
  srv.reply("RPRT -1\n");
}

void rigctlUpdate() { srv.update(); }
