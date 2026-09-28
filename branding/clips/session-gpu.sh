#!/bin/bash
# Container entrypoint for the GPU session (run under dbus-run-session), started
# by thumbs.sh. Unlike session.sh (X11-windowed KWin on Xvfb, QPainter
# compositing), KWin runs its virtual backend and composites with OpenGL, so
# its screencasts work: Krema's live window thumbnails, and the recording
# itself (a zkde_screencast region stream read by GStreamer's pipewiresrc and
# encoded by the VPU, see rec_start in clips.sh).
# Input goes through Xwayland: xdotool's XTEST events reach KWin over libei
# (kwinrc [Xwayland] XwaylandEisNoPrompt=true skips the permission prompt).
#
# - SCALE (default 2): output scale. The logical screen is W x H (default
#   1280x720); the output is SCALE times larger in pixels.
# - MALI (default 1): KWin loads Arm's libmali from /opt/mali (EGL + GBM on
#   the Mali G610, GLES only). MALI=0 leaves it on Mesa, which has no driver
#   for the G610 on the vendor kernel and composites with llvmpipe.
# - SPEED (default 1): real time. Below 1, libfaketime slows every clock in
#   the session, as in session.sh; that is only needed without a GPU.
set -u
export XDG_RUNTIME_DIR=/tmp/rt-$(id -u)
mkdir -p -m 700 "$XDG_RUNTIME_DIR"
export KWIN_WAYLAND_NO_PERMISSION_CHECKS=1
export XDG_CURRENT_DESKTOP=KDE KDE_FULL_SESSION=true KDE_SESSION_VERSION=6
export XCURSOR_THEME=breeze_cursors XCURSOR_SIZE=24
SPEED=${SPEED:-1}
SCALE=${SCALE:-2}
MALI=${MALI:-1}
W=${W:-1280} H=${H:-720}
FAKE=""
if [ "$SPEED" != 1 ]; then
    FAKE="export LD_PRELOAD=/usr/lib64/libfaketimeMT.so.1 FAKETIME='+0 x$SPEED' FAKETIME_DONT_FAKE_MONOTONIC=0"
    eval "$FAKE"
fi
# gles-shim (libGLESv2.so.2, see gles-shim.c) goes first so KWin's shaders
# compile on Mali.
KWIN_ENV=()
if [ "$MALI" = 1 ] && [ -f /opt/mali/libmali.so ]; then
    KWIN_ENV=(LD_LIBRARY_PATH=/usr/local/lib/krema-gles:/opt/mali KWIN_COMPOSE=O2ES)
fi
mkdir -p ~/.config
mkdir -p -m 1777 /tmp/.X11-unix   # KWin puts Xwayland's socket here
printf '[Xwayland]\nXwaylandEisNoPrompt=true\n' >~/.config/kwinrc
pipewire >/tmp/pipewire.log 2>&1 &
sleep 1
wireplumber >/tmp/wireplumber.log 2>&1 &
env "${KWIN_ENV[@]}" QT_FORCE_STDERR_LOGGING=1 \
    QT_LOGGING_RULES="kwin_core.debug=true;kwin_screencast.debug=true;kwin_scripting.debug=true;kwin_libeis.debug=true;js.debug=true" \
    kwin_wayland --virtual --xwayland --no-lockscreen --socket wayland-0 \
    --width "$((W * SCALE))" --height "$((H * SCALE))" >/tmp/kwin.log 2>&1 &
for _ in $(seq 100); do [ -S "$XDG_RUNTIME_DIR/wayland-0" ] && break; sleep 0.2; done
# --scale does not reach the virtual output; set it like session.sh does.
WAYLAND_DISPLAY=wayland-0 kscreen-doctor "output.Virtual-0.scale.$SCALE" >/dev/null 2>&1
# Xwayland's display number (rootless, started by KWin).
for _ in $(seq 100); do
    DISP=$(ls /tmp/.X11-unix 2>/dev/null | sed -n 's/^X\([0-9]*\)$/:\1/p' | head -n 1)
    [ -n "$DISP" ] && break
    sleep 0.2
done
cat >/tmp/session.env <<EOF
export XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR
export DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS
export WAYLAND_DISPLAY=wayland-0
export DISPLAY=$DISP
export QT_QPA_PLATFORM=wayland
# Clients: Mesa has no driver for the G610 on this kernel, Fedora's Qt wants
# desktop OpenGL (libmali has only GLES), and libmali's Vulkan has no Wayland
# WSI; so llvmpipe over wl_shm. Two llvmpipe threads per client: with one per
# host core the container hits its CPU quota (--cpus 4) in bursts and every
# process, KWin included, stalls for the rest of the period.
export LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=${LP_NUM_THREADS:-2}
$([ "$SPEED" = 1 ] || echo "export QSG_USE_SIMPLE_ANIMATION_DRIVER=1")
# jellyfin-ffmpeg (h264_rkmpp) through its own Debian root, see thumbs.sh.
export JF_FFMPEG="/opt/jf/lib/ld-linux-aarch64.so.1 --library-path /opt/jf/usr/lib/jellyfin-ffmpeg/lib:/opt/jf/usr/lib/aarch64-linux-gnu:/opt/jf/lib/aarch64-linux-gnu /opt/jf/usr/lib/jellyfin-ffmpeg/ffmpeg"
# With libmali the recorder takes DMA-BUF frames and reads them back with
# GStreamer's GL elements (EGL on GBM, the render node), see rec_start.
export CAPTURE_GL="$([ ${#KWIN_ENV[@]} -gt 0 ] && echo "LD_LIBRARY_PATH=/usr/local/lib/krema-gles:/opt/mali GST_GL_API=gles2 GST_GL_PLATFORM=egl GST_GL_WINDOW=gbm GST_GL_GBM_DRM_DEVICE=$(ls /dev/dri/renderD* | head -n 1)")"
export QT_FORCE_STDERR_LOGGING=1
export XDG_CURRENT_DESKTOP=KDE
export KDE_FULL_SESSION=true
export KDE_SESSION_VERSION=6
export XCURSOR_THEME=breeze_cursors
export XCURSOR_SIZE=24
export SHELL=/bin/bash
export SCALE=$SCALE W=$W H=$H SPEED=$SPEED GPU=1
$FAKE
EOF
echo READY
wait
