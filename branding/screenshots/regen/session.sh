#!/bin/bash
# Container entrypoint (run under dbus-run-session): nested KWin on Xvfb.
# KWin's --virtual backend has no DRM render node in Docker, falls back to
# QPainter, and then ScreenShot2 cancels every request. As an X11-windowed
# compositor on Xvfb, KWin still composites (QPainter) into an X window, so
# frames are grabbed from the Xvfb root with scrot and input comes from xdotool.
set -u
export XDG_RUNTIME_DIR=/tmp/rt-$(id -u)
mkdir -p -m 700 "$XDG_RUNTIME_DIR"
export KWIN_WAYLAND_NO_PERMISSION_CHECKS=1
export XDG_CURRENT_DESKTOP=KDE KDE_FULL_SESSION=true KDE_SESSION_VERSION=6
W=${W:-1920} H=${H:-1080}
Xvfb :99 -screen 0 "${W}x${H}x24" -nolisten tcp >/tmp/xvfb.log 2>&1 &
for _ in $(seq 50); do [ -e /tmp/.X11-unix/X99 ] && break; sleep 0.2; done
DISPLAY=:99 kwin_wayland --x11-display :99 --no-lockscreen --socket wayland-0 \
    --width "$W" --height "$H" >/tmp/kwin.log 2>&1 &
for _ in $(seq 100); do [ -S "$XDG_RUNTIME_DIR/wayland-0" ] && break; sleep 0.2; done
cat >/tmp/session.env <<EOF
export XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR
export DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS
export WAYLAND_DISPLAY=wayland-0
export QT_QPA_PLATFORM=wayland
export QT_QUICK_BACKEND=software
export QT_FORCE_STDERR_LOGGING=1
export XDG_CURRENT_DESKTOP=KDE
export KDE_FULL_SESSION=true
export KDE_SESSION_VERSION=6
export SHELL=/bin/bash
EOF
echo READY
wait
