#!/usr/bin/env python3
"""Serial monitor for a bare ESP-12F on a USB-UART adapter.

Opening the port with pyserial's defaults gives zero bytes at 115200 while the
ROM bootloader is readable at 74880 - the chip sits in flash mode instead of
running the app, and it looks exactly like a dead board. Clearing DTR before
reading fixes it. Pulsing RTS does NOT reset this board, so the boot banner can
only be caught right after an upload or a manual reset.

Usage: monitor.py [seconds] [baud]   (0 seconds = run until Ctrl-C)
"""
import sys
import time

import serial

PORT = "/dev/cu.usbserial-0001"


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 0
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

    s = serial.Serial(PORT, baud, timeout=0.5)
    s.dtr = False        # the one that matters: without it the chip stays mute
    s.rts = False
    time.sleep(0.15)

    end = time.time() + secs if secs else None
    try:
        while end is None or time.time() < end:
            line = s.readline()
            if line:
                print(line.decode("utf-8", "replace").rstrip(), flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        s.close()


if __name__ == "__main__":
    main()
