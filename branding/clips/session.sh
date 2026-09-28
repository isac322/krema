#!/bin/bash
# Container entrypoint (run under dbus-run-session): nested KWin on Xvfb.
# KWin runs as an X11-windowed compositor on Xvfb, frames come from the Xvfb
# root window (ffmpeg x11grab) and input from xdotool. See NOTES.md.
#
# - SCALE (default 2): output scale. The logical screen is W x H (default
#   1280x720); Xvfb, KWin's output and the recording are SCALE times larger, so
#   the clips have 2x pixel density.
# - SPEED (default 0.1): libfaketime slows every clock in the session (KWin,
#   plasmashell, Krema, the apps, the pointer driver, sleep, the recorder) so
#   the host has 1/SPEED times more wall time per recorded frame.
#   FAKETIME_DONT_FAKE_MONOTONIC=0 is essential: Fedora's libfaketime leaves
#   CLOCK_MONOTONIC alone by default and only stretches CLOCK_REALTIME and
#   sleep/poll timeouts. KWin's render loop, Qt Quick animations and Firefox
#   all run on CLOCK_MONOTONIC, so they ran 1/SPEED times too fast and the
#   recorder sampled them at random phases (the jittery launch bounce).
# - QSG_USE_SIMPLE_ANIMATION_DRIVER=1 (in session.env): Qt Quick animations
#   follow the (slowed) elapsed-time clock instead of counting one 16.7 ms
#   vsync tick per rendered frame, so a dropped frame cannot stretch them.
# - Qt Quick clients render with OpenGL on Mesa llvmpipe
#   (LIBGL_ALWAYS_SOFTWARE=1, buffers over wl_shm). The software renderer
#   cannot draw MultiEffect layers (the notification badge).
set -u
export XDG_RUNTIME_DIR=/tmp/rt-$(id -u)
mkdir -p -m 700 "$XDG_RUNTIME_DIR"
export KWIN_WAYLAND_NO_PERMISSION_CHECKS=1
export XDG_CURRENT_DESKTOP=KDE KDE_FULL_SESSION=true KDE_SESSION_VERSION=6
export XCURSOR_THEME=breeze_cursors XCURSOR_SIZE=24
SPEED=${SPEED:-0.1}
SCALE=${SCALE:-2}
W=${W:-1280} H=${H:-720}
OUTPUTS=${OUTPUTS:-1}
PW=$((W * SCALE)) PH=$((H * SCALE))
FAKE=""
if [ "$SPEED" != 1 ]; then
    FAKE="export LD_PRELOAD=/usr/lib64/libfaketimeMT.so.1 FAKETIME='+0 x$SPEED' FAKETIME_DONT_FAKE_MONOTONIC=0"
    eval "$FAKE"
fi
pipewire >/tmp/pipewire.log 2>&1 &
sleep 1
wireplumber >/tmp/wireplumber.log 2>&1 &
# One Xvfb screen wide enough for OUTPUTS side-by-side KWin output windows.
Xvfb :99 -screen 0 "$((PW * OUTPUTS))x${PH}x24" -nolisten tcp >/tmp/xvfb.log 2>&1 &
for _ in $(seq 50); do [ -e /tmp/.X11-unix/X99 ] && break; sleep 0.2; done
DISPLAY=:99 QT_FORCE_STDERR_LOGGING=1 \
    QT_LOGGING_RULES="kwin_core.debug=true;kwin_screencast.debug=true;kwin_scripting.debug=true;js.debug=true;qml.debug=true" \
    kwin_wayland --x11-display :99 --no-lockscreen --socket wayland-0 \
    --width "$PW" --height "$PH" --output-count "$OUTPUTS" >/tmp/kwin.log 2>&1 &
for _ in $(seq 100); do [ -S "$XDG_RUNTIME_DIR/wayland-0" ] && break; sleep 0.2; done
# kwin_wayland --scale only enlarges the host window; the output scale itself is
# set through the output-management protocol.
for o in $(seq 0 $((OUTPUTS - 1))); do
    WAYLAND_DISPLAY=wayland-0 kscreen-doctor "output.X11-$o.scale.$SCALE" >/dev/null 2>&1
done
cat >/tmp/session.env <<EOF
export XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR
export DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS
export WAYLAND_DISPLAY=wayland-0
export QT_QPA_PLATFORM=wayland
export LIBGL_ALWAYS_SOFTWARE=1
export QSG_USE_SIMPLE_ANIMATION_DRIVER=1
export QT_FORCE_STDERR_LOGGING=1
export XDG_CURRENT_DESKTOP=KDE
export KDE_FULL_SESSION=true
export KDE_SESSION_VERSION=6
export XCURSOR_THEME=breeze_cursors
export XCURSOR_SIZE=24
export SHELL=/bin/bash
export SCALE=$SCALE W=$W H=$H OUTPUTS=$OUTPUTS SPEED=$SPEED
$FAKE
EOF
echo READY
wait
