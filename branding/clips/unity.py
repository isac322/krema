#!/usr/bin/env python3
"""Send com.canonical.Unity.LauncherEntry.Update signals for one app.

usage: unity.py DESKTOP_ID  (e.g. org.kde.dolphin.desktop)

Reads one update per stdin line, KEY=VALUE pairs separated by spaces:
  count=3 count-visible=true progress=0.4 progress-visible=true urgent=false
and sends it as the app would. `ramp=FROM:TO:SECONDS` sends progress updates
at RATE Hz from FROM to TO, paced on the monotonic clock, before the next
line is read. Each ramp logs its start and measured length on stderr.
The process stays on the session bus until stdin closes: docks drop an entry
when its sender leaves the bus.
"""
import sys
import time

import dbus
import dbus.lowlevel

RATE = 30
TYPES = {
    "count": lambda v: dbus.Int64(int(v)),
    "progress": lambda v: dbus.Double(float(v)),
    "count-visible": lambda v: dbus.Boolean(v == "true"),
    "progress-visible": lambda v: dbus.Boolean(v == "true"),
    "urgent": lambda v: dbus.Boolean(v == "true"),
}

bus = dbus.SessionBus()
uri = "application://" + sys.argv[1]


def send(props):
    msg = dbus.lowlevel.SignalMessage("/com/canonical/unity/launcherentry/1", "com.canonical.Unity.LauncherEntry", "Update")
    msg.append(uri, dbus.Dictionary(props, signature="sv"), signature="sa{sv}")
    bus.send_message(msg)
    bus.flush()


def ramp(start, end, seconds):
    n = max(1, round(seconds * RATE))
    t0 = time.monotonic()
    print(f"ramp {start}->{end} start {time.time():.3f}", file=sys.stderr, flush=True)
    for i in range(1, n + 1):
        time.sleep(max(0.0, t0 + i * seconds / n - time.monotonic()))
        send({"progress": dbus.Double(start + (end - start) * i / n)})
    print(f"ramp {start}->{end} took {time.monotonic() - t0:.3f} s", file=sys.stderr, flush=True)


for line in iter(sys.stdin.readline, ""):  # `for line in sys.stdin` would read ahead
    props, steps = {}, None
    for pair in line.split():
        key, value = pair.split("=", 1)
        if key == "ramp":
            steps = [float(v) for v in value.split(":")]
        else:
            props[key] = TYPES[key](value)
    if props:
        send(props)
    if steps:
        ramp(*steps)
