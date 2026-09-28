#!/usr/bin/env python3
"""Pointer glide through XTest, paced on the monotonic clock.

usage: glide.py X1 Y1 X2 Y2 SECONDS   (X11 root pixels)

Moves the pointer from (X1, Y1) to (X2, Y2) with smoothstep easing at 120 Hz.
Each step waits for its own deadline, so the glide lasts SECONDS however long
a step takes (a chain of `xdotool mousemove ... sleep ...` drifts by the time
of every call). Prints the measured length on stderr.
"""
import ctypes
import ctypes.util
import sys
import time

RATE = 120
x1, y1, x2, y2 = (float(v) for v in sys.argv[1:5])
seconds = float(sys.argv[5])

xlib = ctypes.CDLL(ctypes.util.find_library("X11"))
xtst = ctypes.CDLL(ctypes.util.find_library("Xtst"))
xlib.XOpenDisplay.restype = ctypes.c_void_p
xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
xlib.XFlush.argtypes = [ctypes.c_void_p]
xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]

dpy = xlib.XOpenDisplay(None)
if not dpy:
    sys.exit("glide.py: cannot open display")
n = max(1, round(seconds * RATE))
t0 = time.monotonic()
for i in range(1, n + 1):
    time.sleep(max(0.0, t0 + i * seconds / n - time.monotonic()))
    u = i / n
    u = u * u * (3 - 2 * u)
    xtst.XTestFakeMotionEvent(dpy, -1, round(x1 + (x2 - x1) * u), round(y1 + (y2 - y1) * u), 0)
    xlib.XFlush(dpy)
print(f"glide {seconds} s took {time.monotonic() - t0:.3f} s", file=sys.stderr)
xlib.XCloseDisplay(dpy)
