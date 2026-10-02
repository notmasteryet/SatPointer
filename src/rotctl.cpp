#include "rotctl.h"

#include "lineserver.h"
#include "orientation.h"
#include "settings.h"

// How long after a P command we still consider gpredict to be driving, just
// for the display indicator.
static const unsigned long DRIVING_MS = 15000;

static void handleLine(String &cmd);
static LineServer srv(4533, "rotctl", handleLine);
static unsigned long lastSet = 0;

bool rotctlConnected() { return srv.connected(); }
bool rotctlDriving() { return lastSet && millis() - lastSet < DRIVING_MS; }

static void handleLine(String &cmd) {
  // hamlib accepts a long form for every short command.
  if (cmd.startsWith("\\")) cmd = cmd.substring(1);
  char c = cmd[0];
  if (cmd.startsWith("get_pos")) c = 'p';
  else if (cmd.startsWith("set_pos")) c = 'P';
  else if (cmd.startsWith("stop")) c = 'S';
  else if (cmd.startsWith("park")) c = 'K';

  switch (c) {
    case 'p': {
      Angles o;
      if (!orientationAngles(o)) {
        srv.reply("RPRT -1\n");
        return;
      }
      char reply[24];
      snprintf(reply, sizeof reply, "%.1f\n%.1f\n", trimmedAz(o), o.elev);
      srv.reply(reply);
      return;
    }

    case 'P': {
      int sp = cmd.indexOf(' ');
      if (sp < 0) { srv.reply("RPRT -1\n"); return; }
      String rest = cmd.substring(sp + 1);
      rest.trim();
      int sp2 = rest.indexOf(' ');
      if (sp2 < 0) { srv.reply("RPRT -1\n"); return; }

      // Straight into the live target and deliberately not through the
      // encoder's save path: gpredict sends this once a second for the length
      // of a pass, which would be thousands of flash writes.
      cal.targetAz = fmod(rest.substring(0, sp2).toFloat() + 360.0, 360.0);
      cal.targetEl = constrain(rest.substring(sp2 + 1).toFloat(), -90.0, 90.0);
      lastSet = millis();
      srv.reply("RPRT 0\n");
      return;
    }

    case 'S':  // stop
    case 'K':  // park
      // Nothing to stop - a human is the drive train - but refusing would make
      // gpredict think the rotator is broken.
      srv.reply("RPRT 0\n");
      return;

    case 'q':
    case 'Q':
      srv.close();
      return;

    case '_':
      srv.reply("SatPointer\n");
      return;

    default:
      // Logged rather than silently refused: the first real gpredict session
      // is what tells us whether anything else is needed.
      Serial.printf("# rotctl: unhandled '%s'\n", cmd.c_str());
      srv.reply("RPRT -1\n");
      return;
  }
}

void rotctlUpdate() { srv.update(); }
