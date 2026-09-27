#!/bin/bash
# Container entrypoint (run under dbus-run-session): nested KWin on Xvfb.
# Same approach as branding/screenshots/regen/session.sh (see NOTES.md there):
# KWin runs as an X11-windowed compositor on Xvfb, frames come from the Xvfb
# root window (ffmpeg x11grab) and input from xdotool.
#
# Differences from the stills pipeline:
# - Clients render Qt Quick with OpenGL on Mesa llvmpipe (LIBGL_ALWAYS_SOFTWARE=1,
#   buffers go over wl_shm) instead of QT_QUICK_BACKEND=software. The software
#   renderer cannot draw MultiEffect layers, so Krema's numbered notification
#   badge was invisible with it.
# - SPEED (default 0.2) slows every clock in the session with libfaketime:
#   KWin, plasmashell, Krema, the apps, the pointer driver, sleep, and the ffmpeg
#   recorder all run on the same slowed clock. The recorder timestamps frames
#   in that clock, so the recording plays back at true speed while the host has
#   1/SPEED times more wall time per frame. This keeps motion smooth on a busy
#   host. SPEED=1 disables it.
set -u
export XDG_RUNTIME_DIR=/tmp/rt-$(id -u)
mkdir -p -m 700 "$XDG_RUNTIME_DIR"
export KWIN_WAYLAND_NO_PERMISSION_CHECKS=1
export XDG_CURRENT_DESKTOP=KDE KDE_FULL_SESSION=true KDE_SESSION_VERSION=6
export XCURSOR_THEME=breeze_cursors XCURSOR_SIZE=24
SPEED=${SPEED:-0.2}
FAKE=""
if [ "$SPEED" != 1 ]; then
    FAKE="export LD_PRELOAD=/usr/lib64/libfaketimeMT.so.1 FAKETIME='+0 x$SPEED'"
    eval "$FAKE"
fi
W=${W:-1920} H=${H:-1080}
Xvfb :99 -screen 0 "${W}x${H}x24" -nolisten tcp >/tmp/xvfb.log 2>&1 &
for _ in $(seq 50); do [ -e /tmp/.X11-unix/X99 ] && break; sleep 0.2; done
DISPLAY=:99 QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES="kwin_scripting.debug=true;js.debug=true;qml.debug=true" \
    kwin_wayland --x11-display :99 --no-lockscreen --socket wayland-0 --width "$W" --height "$H" >/tmp/kwin.log 2>&1 &
for _ in $(seq 100); do [ -S "$XDG_RUNTIME_DIR/wayland-0" ] && break; sleep 0.2; done
cat >/tmp/session.env <<EOF
export XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR
export DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS
export WAYLAND_DISPLAY=wayland-0
export QT_QPA_PLATFORM=wayland
export LIBGL_ALWAYS_SOFTWARE=1
export QT_FORCE_STDERR_LOGGING=1
export XDG_CURRENT_DESKTOP=KDE
export KDE_FULL_SESSION=true
export KDE_SESSION_VERSION=6
export XCURSOR_THEME=breeze_cursors
export XCURSOR_SIZE=24
export SHELL=/bin/bash
$FAKE
EOF
echo READY
wait
