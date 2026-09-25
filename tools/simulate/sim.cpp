// Renders the real guidance screen as text so the layout can be checked
// without a device. Build: see tools/simulate/run.sh
#include "Adafruit_GFX.h"
#include "../../src/guidance.h"
#include <cstdio>

static void render(const char *title, const Guidance &d) {
  Adafruit_GFX g;
  drawGuidance(g, d);
  printf("\n== %s ==\n", title);
  printf("   +%.*s+\n", 128, "--------------------------------------------------------------------------------------------------------------------------------");
  for (int y = 0; y < 64; y += 2) {
    printf("%s|", y < 16 ? " Y " : " C ");
    for (int x = 0; x < 128; x++) {
      int t = g.px[y][x], b = g.px[y+1][x];
      putchar(t && b ? '#' : t ? '\'' : b ? '.' : ' ');
    }
    printf("|%s\n", y == 14 ? "  <- yellow/cyan boundary" : "");
  }
  printf("   +%.*s+\n", 128, "--------------------------------------------------------------------------------------------------------------------------------");
  for (auto &t : g.texts) printf("   text %s\n", t.c_str());
  // anything drawn outside the panel is a bug the device would silently clip
  printf("   %s\n", "");
}

int main() {
  Guidance a{true, 90.0f, 10.0f, 145.0f, 28.5f, 55.0f, 18.5f, "BAD 4%", false};
  render("turn right and up", a);

  Guidance b{true, 200.0f, 40.0f, 110.0f, 12.0f, -90.0f, -28.0f, "ok 2%", true};
  render("turn left and down, gpredict driving", b);

  Guidance c{true, 145.0f, 28.5f, 145.0f, 28.5f, 0.1f, -0.2f, "ok 2%", false};
  render("on target", c);

  Guidance d{true, 10.0f, 87.0f, 350.0f, 89.0f, -20.0f, 2.0f, "NO CAL", false};
  render("near vertical", d);

  Guidance f{true, 300.0f, -25.0f, 300.0f, -40.0f, 0.0f, -15.0f, "ok 2%", false};
  render("below the horizon", f);

  Guidance g2{true, 0.0f, -90.0f, 180.0f, 90.0f, 180.0f, 180.0f, "ok 2%", false};
  render("extremes: straight down to straight up", g2);

  Guidance e{false, 0, 0, 0, 0, 0, 0, "NO CAL", false};
  render("no fix", e);
  return 0;
}
