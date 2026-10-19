#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
# Run inside an isolated Appium image with Xvfb, xdotool and ffmpeg installed.
# /src is read-only source; /artifacts is writable; KREMA_E2E_BINARY is built.
set -euo pipefail

: "${GITHUB_SHA:?Set GITHUB_SHA to the revision being recorded}"
export KREMA_E2E_BINARY="${KREMA_E2E_BINARY:-/build/bin/krema}"
export KREMA_RECORD_KWIN_BINARY
KREMA_RECORD_KWIN_BINARY=$(command -v kwin_wayland)
mkdir -p /artifacts /tmp/krema-record-bin
cat >/tmp/krema-record-bin/kwin_wayland <<'SHIM'
#!/bin/bash
set -eu
args=()
for arg in "$@"; do
    if [ "$arg" != --virtual ]; then args+=("$arg"); fi
done
export DISPLAY=:99
exec "$KREMA_RECORD_KWIN_BINARY" --x11-display :99 "${args[@]}"
SHIM
chmod +x /tmp/krema-record-bin/kwin_wayland
export DISPLAY=:99
Xvfb :99 -screen 0 1280x900x24 -nolisten tcp >/artifacts/xvfb.log 2>&1 &
xvfb_pid=$!
recorder_pid=
cleanup() {
    if [ -n "$recorder_pid" ]; then
        kill -INT "$recorder_pid" 2>/dev/null || true
        wait "$recorder_pid" || true
    fi
    kill "$xvfb_pid" 2>/dev/null || true
}
trap cleanup EXIT
for _ in $(seq 1 100); do
    if [ -S /tmp/.X11-unix/X99 ]; then break; fi
    sleep 0.05
done
test -S /tmp/.X11-unix/X99
export PATH="/tmp/krema-record-bin:$PATH"
export KREMA_E2E_SCREEN_WIDTH=1280
export KREMA_E2E_SCREEN_HEIGHT=900
export KREMA_RECORD_EXTERNAL=1
export KREMA_RECORD_X11_SCALE=1
export PYTHONDONTWRITEBYTECODE=1
unset LD_PRELOAD FAKETIME FAKETIME_DONT_FAKE_MONOTONIC
sha256sum "$KREMA_E2E_BINARY" >/artifacts/recorded-binary.sha256
printf '%s\n' "$GITHUB_SHA" >/artifacts/recorded-revision.txt
python3 -c 'import time; print(time.monotonic_ns())' >/artifacts/capture-start-ns.txt
ffmpeg -y -hide_banner -loglevel warning -f x11grab -draw_mouse 1 -video_size 1280x900 \
    -framerate 30 -i :99 -t 180 -an -c:v libopenh264 -b:v 3M -pix_fmt yuv420p \
    -movflags +faststart /artifacts/notifications.mp4 >/artifacts/ffmpeg.log 2>&1 &
recorder_pid=$!
set +e
timeout 180 sh /src/tests/appium/entrypoint.sh record_notifications.py -q >/artifacts/runner.log 2>&1
rc=$?
python3 -c 'import time; print(time.monotonic_ns())' >/artifacts/capture-end-ns.txt
kill -INT "$recorder_pid" 2>/dev/null
wait "$recorder_pid"
video_rc=$?
recorder_pid=
set -e
printf '%s\n' "$rc" >/artifacts/capture.exit
if [ "$rc" -ne 0 ]; then exit "$rc"; fi
if [ "$video_rc" -ne 0 ] && [ "$video_rc" -ne 255 ]; then exit "$video_rc"; fi
test -s /artifacts/notifications.mp4
ffprobe -v error -select_streams v:0 -count_frames \
    -show_entries stream=nb_read_frames:format=duration -of json \
    /artifacts/notifications.mp4 >/artifacts/video-probe.json
