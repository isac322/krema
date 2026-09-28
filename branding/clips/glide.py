#!/usr/bin/env python3
"""Pointer glide through XTest, paced on the monotonic clock.

usage: glide.py X1 Y1 X2 Y2 SECONDS [--hold S]   (X11 root pixels)

Moves the pointer from (X1, Y1) to (X2, Y2) with smoothstep easing at 120 Hz.
Each step waits for its own deadline, so the glide lasts SECONDS however long
a step takes (a chain of `xdotool mousemove ... sleep ...` drifts by the time
of every call). Prints the measured length on stderr.

--hold S: a drag. Press button 1 at (X1, Y1), wait S, glide, release. Press,
motion and release must come from one X connection: Xwayland gives every
XTEST client its own libei device and releases its buttons when the client
disconnects, so an `xdotool mousedown` is undone as soon as xdotool exits.
"""
import argparse
import ctypes
import ctypes.util
import sys
import time

RATE = 120
ap = argparse.ArgumentParser()
for name in ("x1", "y1", "x2", "y2", "seconds"):
    ap.add_argument(name, type=float)
ap.add_argument("--hold", type=float)
args = ap.parse_args()
x1, y1, x2, y2, seconds = args.x1, args.y1, args.x2, args.y2, args.seconds

xlib = ctypes.CDLL(ctypes.util.find_library("X11"))
xtst = ctypes.CDLL(ctypes.util.find_library("Xtst"))
xlib.XOpenDisplay.restype = ctypes.c_void_p
xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
xlib.XFlush.argtypes = [ctypes.c_void_p]
xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]

dpy = xlib.XOpenDisplay(None)
if not dpy:
    sys.exit("glide.py: cannot open display")


def button(down):
    xtst.XTestFakeButtonEvent(dpy, 1, down, 0)
    xlib.XFlush(dpy)


if args.hold is not None:
    xtst.XTestFakeMotionEvent(dpy, -1, round(x1), round(y1), 0)
    button(True)
    time.sleep(args.hold)
n = max(1, round(seconds * RATE))
t0 = time.monotonic()
for i in range(1, n + 1):
    time.sleep(max(0.0, t0 + i * seconds / n - time.monotonic()))
    u = i / n
    u = u * u * (3 - 2 * u)
    xtst.XTestFakeMotionEvent(dpy, -1, round(x1 + (x2 - x1) * u), round(y1 + (y2 - y1) * u), 0)
    xlib.XFlush(dpy)
print(f"glide {seconds} s took {time.monotonic() - t0:.3f} s", file=sys.stderr)
if args.hold is not None:
    time.sleep(0.3)   # let the last motion land before the drop
    button(False)
xlib.XCloseDisplay(dpy)
