#!/bin/sh
# Render the guidance screen on the host. The mock in this directory stands in
# for Adafruit_GFX so the code that actually ships is what gets rendered -
# layout bugs on a 128x64 panel are otherwise only findable by flashing and
# squinting, and the panel is two-colour so the band boundary matters.
set -e
cd "$(dirname "$0")/../.."
c++ -std=c++14 -I tools/simulate -o /tmp/satpointer-sim tools/simulate/sim.cpp src/guidance.cpp
exec /tmp/satpointer-sim
