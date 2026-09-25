#include "guidance.h"

#include <math.h>

#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295
#endif

static const int16_t W = 128;

// Azimuth: a compass seen from above, north up. Elevation: the same beam seen
// from the side. Both draw the beam as a line with the device as a crossbar on
// it, so the picture matches what is in your hands.
// This is a two-colour panel: the top 16 rows are yellow, the remaining 48
// cyan, with a physical gap between them. Nothing may straddle that line, so
// the yellow strip is the header and the graphics live entirely below it.
static const int16_t BAND = 16;

// Both views are vertically centred in the cyan band, because each arm has to
// swing a full -90..+90 and a baseline pivot puts half of that off the panel.
static const int16_t CX = 32, CY = 40, CR = 18;   // compass, left half
static const int16_t EX = 96, EY = 40, ER = 18;   // elevation, right half

static void polar(int16_t cx, int16_t cy, float deg, float r, int16_t &x, int16_t &y) {
  float a = deg * DEG_TO_RAD;
  x = cx + (int16_t)lroundf(sinf(a) * r);
  y = cy - (int16_t)lroundf(cosf(a) * r);
}

// A short bar across the beam: the device clamped to it.
static void crossbar(Adafruit_GFX &g, int16_t cx, int16_t cy, float deg, float r, int16_t half) {
  int16_t ax, ay, bx, by;
  polar(cx, cy, deg + 90, half, ax, ay);
  polar(cx, cy, deg - 90, half, bx, by);
  int16_t px, py;
  polar(cx, cy, deg, r, px, py);
  g.drawLine(px + (ax - cx), py + (ay - cy), px + (bx - cx), py + (by - cy), 1);
}

// Arrowhead pointing along `deg`, drawn at (x, y).
static void arrowHead(Adafruit_GFX &g, int16_t x, int16_t y, float deg, int16_t size) {
  int16_t lx, ly, rx, ry;
  polar(0, 0, deg + 140, size, lx, ly);
  polar(0, 0, deg - 140, size, rx, ry);
  g.fillTriangle(x, y, x + lx, y + ly, x + rx, y + ry, 1);
}

// The instruction itself: an arc inside the compass sweeping the way the beam
// has to turn, with the head on the end you are turning towards.
static void turnArc(Adafruit_GFX &g, float from, float delta) {
  if (fabsf(delta) < 1.0) return;
  const float r = 9;
  const int steps = 8;
  int16_t px = 0, py = 0;
  for (int i = 0; i <= steps; i++) {
    int16_t x, y;
    polar(CX, CY, from + delta * i / steps, r, x, y);
    if (i) g.drawLine(px, py, x, y, 1);
    px = x;
    py = y;
  }
  // Tangent at the end, which is the direction of travel.
  arrowHead(g, px, py, from + delta + (delta > 0 ? 90 : -90), 4);
}

static void drawCompass(Adafruit_GFX &g, const Guidance &d) {
  g.drawCircle(CX, CY, CR, 1);
  // North tick on the rim rather than a letter: there is no room for text in
  // the cyan band without it colliding with the compass.
  g.drawLine(CX, CY - CR - 2, CX, CY - CR + 2, 1);

  // Target: a tick outside the rim so it cannot be confused with the beam.
  int16_t ax, ay, bx, by;
  polar(CX, CY, d.targetAz, CR + 1, ax, ay);
  polar(CX, CY, d.targetAz, CR + 5, bx, by);
  g.drawLine(ax, ay, bx, by, 1);
  arrowHead(g, ax, ay, d.targetAz + 180, 3);

  // Beam, with the device on it.
  polar(CX, CY, d.az, CR - 2, ax, ay);
  g.drawLine(CX, CY, ax, ay, 1);
  crossbar(g, CX, CY, d.az, CR * 0.62, 4);
  g.fillCircle(CX, CY, 2, 1);

  turnArc(g, d.az, d.dAz);
}

static void drawElevation(Adafruit_GFX &g, const Guidance &d) {
  g.drawFastHLine(EX - ER - 4, EY, 2 * ER + 8, 1);  // horizon
  if (!d.fix) return;

  // Side view: elevation measured up from the horizon, so the screen angle is
  // the negative of the compass convention.
  int16_t ax, ay, bx, by;
  polar(EX, EY, 90 - d.targetEl, ER + 1, ax, ay);
  polar(EX, EY, 90 - d.targetEl, ER + 5, bx, by);
  g.drawLine(ax, ay, bx, by, 1);
  arrowHead(g, ax, ay, 90 - d.targetEl + 180, 3);

  polar(EX, EY, 90 - d.el, ER - 2, ax, ay);
  g.drawLine(EX, EY, ax, ay, 1);
  crossbar(g, EX, EY, 90 - d.el, ER * 0.62, 4);
  g.fillCircle(EX, EY, 2, 1);

  // Which way to swing it, drawn beside the arm rather than along it so it
  // does not sit on top of the beam.
  if (fabsf(d.dEl) >= 1.0) {
    int16_t hx, hy;
    polar(EX, EY, 90 - d.el, ER * 0.82, hx, hy);
    arrowHead(g, hx, hy, d.dEl > 0 ? (90 - d.el) - 90 : (90 - d.el) + 90, 4);
  }
}

static void rightAt(Adafruit_GFX &g, int16_t y, const char *s) {
  if (!s || !*s) return;
  g.setCursor(W - (int16_t)strlen(s) * 6, y);
  g.print(s);
}

// The yellow strip carries what you act on. The picture below says which way
// to turn; these say how far, and what the numbers currently are.
static void drawHeader(Adafruit_GFX &g, const Guidance &d) {
  char line[26];
  bool azOn = fabsf(d.dAz) < 0.5, elOn = fabsf(d.dEl) < 0.5;
  if (azOn && elOn) {
    snprintf(line, sizeof(line), "ON TARGET");
  } else {
    snprintf(line, sizeof(line), "%c%5.1f %c%5.1f",
             azOn ? '=' : (d.dAz > 0 ? '>' : '<'), (double)fabsf(d.dAz),
             elOn ? '=' : (d.dEl > 0 ? '^' : 'v'), (double)fabsf(d.dEl));
  }
  g.setCursor(0, 0);
  g.print(line);
  rightAt(g, 0, d.status);

  snprintf(line, sizeof(line), "AZ%6.1f EL%6.1f", (double)d.az, (double)d.el);
  g.setCursor(0, 8);
  g.print(line);
  if (d.net) rightAt(g, 8, "NET");
}

void drawGuidance(Adafruit_GFX &g, const Guidance &d) {
  g.setTextSize(1);

  if (!d.fix) {
    g.setCursor(0, 0);
    g.print("no fix");
    rightAt(g, 0, d.status);
    return;
  }

  drawHeader(g, d);
  drawCompass(g, d);
  drawElevation(g, d);
}
