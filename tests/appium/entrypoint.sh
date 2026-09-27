#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Container-side entrypoint of tests/appium/run-e2e.sh.
#
#   /src        krema checkout, read-only
#   /work       writable copy of /src (rsync, incremental)
#   /build      named volume: krema build tree (incremental)
#   /artifacts  host tests/appium/artifacts
#
# Usage: entrypoint.sh [pytest args...]
#        entrypoint.sh --shell        (debug shell after the krema build)
#        entrypoint.sh --inner ...    (internal: pytest inside the session)

set -eu

# Inner mode: runs inside the kwin session started below. kwin sends its
# children's stdout to its own log, so pytest writes to a file that the outer
# half streams to the console.
if [ "${1:-}" = "--inner" ]; then
    shift
    # Clients (krema, fixtures) render with llvmpipe whatever kwin's backend
    # is. With the DRM backend kwin runs without this variable (run.rb would
    # add --virtual), and Mesa on vkms would otherwise try zink/dri2 first.
    export LIBGL_ALWAYS_SOFTWARE=1
    cd /work/tests/appium
    python3 -m pytest -p no:cacheprovider --junitxml=/artifacts/junit.xml "$@" >>/artifacts/pytest.log 2>&1
    exit $?
fi

# Artifacts are written as root; hand them back to the host user.
fix_owner() {
    if [ -n "${HOST_UID:-}" ]; then
        chown -R "$HOST_UID:${HOST_GID:-$HOST_UID}" /artifacts 2>/dev/null || true
    fi
}
trap fix_owner EXIT

export XDG_RUNTIME_DIR=/tmp/runtime-root
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

stamp() { date +%s.%N; }
elapsed() { awk -v a="$1" -v b="$(stamp)" 'BEGIN { printf "%.1f", b - a }'; }

mkdir -p /work /artifacts
t0=$(stamp)
rsync -a --delete \
    --exclude '/.git/' --exclude '/build/' --exclude '/tests/appium/artifacts/' \
    --exclude '__pycache__/' --exclude '.pytest_cache/' \
    /src/ /work/
echo "[e2e] source sync: $(elapsed "$t0")s"

t0=$(stamp)
if [ ! -f /build/build.ninja ]; then
    cmake -S /work -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DBUILD_TESTING=OFF \
        >/artifacts/cmake-configure.log 2>&1 || { cat /artifacts/cmake-configure.log; exit 1; }
fi
if ! cmake --build /build --target krema >/artifacts/krema-build.log 2>&1; then
    tail -n 80 /artifacts/krema-build.log
    exit 1
fi
echo "[e2e] krema build: $(elapsed "$t0")s"

KREMA_E2E_BINARY=$(find /build -type f -name krema -perm -u+x -path '*/bin/*' | head -n 1)
[ -n "$KREMA_E2E_BINARY" ] || { echo "krema binary not found under /build" >&2; exit 1; }
export KREMA_E2E_BINARY

if [ "${1:-}" = "--shell" ]; then
    exec bash
fi

# PipeWire must be up before kwin starts, otherwise kwin's screencast
# interface (used by KPipeWire previews) never connects.
pipewire >/artifacts/pipewire.log 2>&1 &
tries=0
until [ -S "$XDG_RUNTIME_DIR/pipewire-0" ]; do
    tries=$((tries + 1))
    [ "$tries" -le 100 ] || { echo "pipewire did not start" >&2; cat /artifacts/pipewire.log >&2; exit 1; }
    sleep 0.05
done
wireplumber >/artifacts/wireplumber.log 2>&1 &

# selenium-webdriver-at-spi-run: private dbus session -> nested
# kwin_wayland --virtual -> at-spi bus -> flask webdriver on :4723 -> our
# command. Everything below inherits WAYLAND_DISPLAY and KWIN_PID from it.
export APPIUM_ARTIFACT_OUTPUT_PATH=/artifacts
export USE_CUSTOM_BUS=1
# KWin backend. `--virtual` (what run.rb passes when LIBGL_ALWAYS_SOFTWARE
# is set) composites with OpenGL only on a device with a render node (vgem);
# otherwise it falls back to QPainter (no screenshots, no screencast). On a
# KMS-only card (vkms, e.g. GitHub's Azure kernels, which lack vgem) kwin's
# DRM backend drives the card itself and composites with OpenGL through
# Mesa's kms_swrast/llvmpipe. Override with KREMA_E2E_KWIN_BACKEND.
kwin_backend=${KREMA_E2E_KWIN_BACKEND:-auto}
if [ "$kwin_backend" = auto ]; then
    kwin_backend=virtual
    if ! ls /dev/dri/renderD* >/dev/null 2>&1 && ls /dev/dri/card* >/dev/null 2>&1; then
        kwin_backend=drm
    fi
fi
# Normalized before the backend case: drm uses it to pick a card, virtual to
# set --output-count.
export KREMA_E2E_OUTPUT_COUNT="${KREMA_E2E_OUTPUT_COUNT:-1}"
case "$kwin_backend" in
virtual)
    export LIBGL_ALWAYS_SOFTWARE=1
    # Opt-in multi-output session (tools/run-output-count.patch): --output-count
    # is a virtual-backend option, forwarded only here. Outputs of
    # SCREEN_WIDTH x SCREEN_HEIGHT each, placed side by side left to right.
    if [ "$KREMA_E2E_OUTPUT_COUNT" -gt 1 ]; then
        export COMPOSITOR_OUTPUT_COUNT="$KREMA_E2E_OUTPUT_COUNT"
    fi
    ;;
drm)
    # The output size is each connector's preferred mode (vkms: 1024x768);
    # --width/--height/--output-count do not apply. More outputs come from
    # more connectors on the card (host: tests/appium/setup-vkms.sh, the vkms
    # configfs ABI on kernel >= 6.19).
    if [ "${KREMA_E2E_SCREEN_WIDTH:-1024}x${KREMA_E2E_SCREEN_HEIGHT:-768}" != 1024x768 ]; then
        echo "KREMA_E2E_KWIN_BACKEND=drm cannot change the 1024x768 output size" >&2
        exit 1
    fi
    # Pick the card with exactly KREMA_E2E_OUTPUT_COUNT connected connectors.
    # Prefer vkms: hosts may have other KMS cards (GitHub runners: hyperv_drm).
    if [ -z "${KWIN_DRM_DEVICES:-}" ]; then
        vkms_card=
        other_card=
        for card in /dev/dri/card*; do
            n=0
            for conn in "/sys/class/drm/${card##*/}"-*/status; do
                [ -f "$conn" ] || continue
                [ "$(cat "$conn")" = connected ] && n=$((n + 1))
            done
            [ "$n" -eq "$KREMA_E2E_OUTPUT_COUNT" ] || continue
            case "$(readlink -f "/sys/class/drm/${card##*/}/device" 2>/dev/null)" in
            */vkms|*/faux/*) [ -n "$vkms_card" ] || vkms_card=$card ;;
            *) [ -n "$other_card" ] || other_card=$card ;;
            esac
        done
        KWIN_DRM_DEVICES=${vkms_card:-$other_card}
    fi
    if [ -z "${KWIN_DRM_DEVICES:-}" ]; then
        echo "no /dev/dri card with $KREMA_E2E_OUTPUT_COUNT connected output(s)" >&2
        echo "with vkms the host can add one: sudo tests/appium/setup-vkms.sh $KREMA_E2E_OUTPUT_COUNT" >&2
        exit 1
    fi
    export KWIN_DRM_DEVICES
    ;;
*)
    echo "KREMA_E2E_KWIN_BACKEND must be auto, virtual or drm" >&2
    exit 1
    ;;
esac
echo "[e2e] kwin backend: $kwin_backend${KWIN_DRM_DEVICES:+ ($KWIN_DRM_DEVICES)}"
export COMPOSITOR_WIDTH="${KREMA_E2E_SCREEN_WIDTH:-1024}"
export COMPOSITOR_HEIGHT="${KREMA_E2E_SCREEN_HEIGHT:-768}"
export KREMA_E2E_SCREEN_WIDTH="$COMPOSITOR_WIDTH"
export KREMA_E2E_SCREEN_HEIGHT="$COMPOSITOR_HEIGHT"
# Upstream passes --no-global-shortcuts by default. Krema needs KWin's
# in-process kglobalaccel: it provides org.kde.kglobalaccel (invokeShortcut)
# and routes real key combos to KGlobalAccel.
export TEST_WITHOUT_GLOBAL_SHORTCUTS=0
export TEST_WITH_VIDEO_RECORDER=0
export QT_FORCE_STDERR_LOGGING=1
export LANGUAGE=C

: >/artifacts/pytest.log
t0=$(stamp)
selenium-webdriver-at-spi-run sh /src/tests/appium/entrypoint.sh --inner "$@" >/artifacts/session.log 2>&1 &
session_pid=$!
# Stream pytest output live; --pid makes tail drain the file and exit once
# the session is gone.
tail -n +1 -s 0.2 -F --pid="$session_pid" /artifacts/pytest.log 2>/dev/null || true
set +e
wait "$session_pid"
rc=$?
set -e
echo "[e2e] tests: $(elapsed "$t0")s (exit $rc)"
exit "$rc"
