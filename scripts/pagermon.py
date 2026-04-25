#!/usr/bin/env python3
"""Long-running serial monitor for Pager USB-CDC. Reconnects on drops."""
import os
import sys
import time
import serial

PORT = "/dev/cu.usbmodem101"

while True:
    try:
        if not os.path.exists(PORT):
            time.sleep(0.2)
            continue
        s = serial.Serial(PORT, 115200, timeout=0.5, dsrdtr=False, rtscts=False)
        sys.stdout.write(f"[mon] opened {PORT}\n")
        sys.stdout.flush()
    except Exception as e:
        sys.stdout.write(f"[mon] open failed: {e}\n")
        sys.stdout.flush()
        time.sleep(0.5)
        continue
    buf = b""
    try:
        while True:
            chunk = s.read(512)
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    sys.stdout.write(
                        f"[{time.time():.2f}] {line.decode('utf-8', errors='replace')}\n"
                    )
                    sys.stdout.flush()
    except Exception as e:
        sys.stdout.write(f"[mon] read failed: {e}\n")
        sys.stdout.flush()
        try:
            s.close()
        except Exception:
            pass
        time.sleep(0.5)
