#include "rotctl.h"

#include <ESP8266WiFi.h>

#include "net.h"
#include "orientation.h"
#include "settings.h"

static const uint16_t PORT = 4533;
// How long after a P command we still consider gpredict to be driving, just
// for the display indicator.
static const unsigned long DRIVING_MS = 15000;

static WiFiServer server(PORT);
static WiFiClient client;
static bool started = false;
static String line;
static unsigned long lastSet = 0;

bool rotctlConnected() { return client && client.connected(); }
bool rotctlDriving() { return lastSet && millis() - lastSet < DRIVING_MS; }

static void handleLine(String cmd) {
  cmd.trim();
  if (!cmd.length()) return;

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
        client.print(F("RPRT -1\n"));
        return;
      }
      // One write, not four: with Nagle off each print is its own packet, and
      // gpredict does a single recv() and wants both lines in it - a reply
      // split after the azimuth shows as ERROR when you press Engage.
      char reply[24];
      snprintf(reply, sizeof reply, "%.1f\n%.1f\n", trimmedAz(o), o.elev);
      client.write(reply, strlen(reply));
      return;
    }

    case 'P': {
      int sp = cmd.indexOf(' ');
      if (sp < 0) { client.print(F("RPRT -1\n")); return; }
      String rest = cmd.substring(sp + 1);
      rest.trim();
      int sp2 = rest.indexOf(' ');
      if (sp2 < 0) { client.print(F("RPRT -1\n")); return; }

      // Straight into the live target and deliberately not through the
      // encoder's save path: gpredict sends this once a second for the length
      // of a pass, which would be thousands of flash writes.
      cal.targetAz = fmod(rest.substring(0, sp2).toFloat() + 360.0, 360.0);
      cal.targetEl = constrain(rest.substring(sp2 + 1).toFloat(), -90.0, 90.0);
      lastSet = millis();
      client.print(F("RPRT 0\n"));
      return;
    }

    case 'S':  // stop
    case 'K':  // park
      // Nothing to stop - a human is the drive train - but refusing would make
      // gpredict think the rotator is broken.
      client.print(F("RPRT 0\n"));
      return;

    case 'q':
    case 'Q':
      client.stop();
      return;

    case '_':
      client.print(F("SatPointer\n"));
      return;

    default:
      // Logged rather than silently refused: the first real gpredict session
      // is what tells us whether anything else is needed.
      Serial.printf("# rotctl: unhandled '%s'\n", cmd.c_str());
      client.print(F("RPRT -1\n"));
      return;
  }
}

void rotctlUpdate() {
  if (netState() != NET_ONLINE) {
    if (started) {
      client.stop();
      server.stop();
      started = false;
    }
    return;
  }

  if (!started) {
    server.begin();
    server.setNoDelay(true);
    started = true;
    Serial.printf("# rotctl: listening on %u\n", PORT);
  }

  if (!client || !client.connected()) {
    WiFiClient fresh = server.available();
    if (fresh) {
      if (client) client.stop();
      client = fresh;
      client.setNoDelay(true);
      line = "";
      Serial.println(F("# rotctl: client connected"));
    }
  }

  while (client && client.connected() && client.available()) {
    char ch = client.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      String done = line;
      line = "";
      handleLine(done);
    } else if (line.length() < 80) {
      line += ch;
    }
  }
}
