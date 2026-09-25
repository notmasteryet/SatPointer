// Host stand-in for Adafruit_GFX, just the primitives guidance.cpp uses, so
// the real drawing code can be rendered and checked without hardware.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

class Adafruit_GFX {
 public:
  static const int W = 128, H = 64;
  uint8_t px[H][W] = {};
  int cx = 0, cy = 0, tsize = 1;
  std::vector<std::string> texts;   // what was printed and where

  void pixel(int x, int y) { if (x >= 0 && x < W && y >= 0 && y < H) px[y][x] = 1; }
  void drawPixel(int x, int y, uint16_t) { pixel(x, y); }

  void drawLine(int x0, int y0, int x1, int y1, uint16_t) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
      pixel(x0, y0);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
  void drawFastHLine(int x, int y, int w, uint16_t) { for (int i = 0; i < w; i++) pixel(x + i, y); }
  void drawFastVLine(int x, int y, int h, uint16_t) { for (int i = 0; i < h; i++) pixel(x, y + i); }
  void drawRect(int x, int y, int w, int h, uint16_t c) {
    drawFastHLine(x, y, w, c); drawFastHLine(x, y + h - 1, w, c);
    drawFastVLine(x, y, h, c); drawFastVLine(x + w - 1, y, h, c);
  }
  void fillRect(int x, int y, int w, int h, uint16_t) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) pixel(x + i, y + j);
  }
  void drawCircle(int x0, int y0, int r, uint16_t) {
    for (double a = 0; a < 6.2832; a += 0.02)
      pixel(x0 + (int)lround(cos(a) * r), y0 + (int)lround(sin(a) * r));
  }
  void fillCircle(int x0, int y0, int r, uint16_t) {
    for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++)
      if (i * i + j * j <= r * r) pixel(x0 + i, y0 + j);
  }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t) {
    int minx = std::min({x0,x1,x2}), maxx = std::max({x0,x1,x2});
    int miny = std::min({y0,y1,y2}), maxy = std::max({y0,y1,y2});
    auto side = [](int ax,int ay,int bx,int by,int x,int y){ return (bx-ax)*(y-ay)-(by-ay)*(x-ax); };
    for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
      long a = side(x0,y0,x1,y1,x,y), b = side(x1,y1,x2,y2,x,y), c = side(x2,y2,x0,y0,x,y);
      if ((a>=0&&b>=0&&c>=0)||(a<=0&&b<=0&&c<=0)) pixel(x,y);
    }
  }
  void drawTriangle(int x0,int y0,int x1,int y1,int x2,int y2,uint16_t c){
    drawLine(x0,y0,x1,y1,c); drawLine(x1,y1,x2,y2,c); drawLine(x2,y2,x0,y0,c);
  }

  void setTextSize(int s) { tsize = s; }
  void setTextColor(uint16_t) {}
  void setCursor(int x, int y) { cx = x; cy = y; }
  // Text is drawn as its real 6x8 footprint so overlap and overflow show up.
  void emit(const std::string &s) {
    char buf[80];
    snprintf(buf, sizeof(buf), "(%3d,%2d) w=%2d \"%s\"", cx, cy, (int)s.size()*6*tsize, s.c_str());
    texts.push_back(buf);
    for (size_t i = 0; i < s.size(); i++)
      for (int j = 0; j < 7*tsize; j++)
        for (int k = 0; k < 5*tsize; k++) pixel(cx + i*6*tsize + k, cy + j);
    cx += s.size() * 6 * tsize;
  }
  void print(const char *s) { emit(s); }
  void print(char c) { emit(std::string(1, c)); }
};
