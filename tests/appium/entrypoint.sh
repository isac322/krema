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
# With KREMA_E2E_BINARY set (an installed krema, tests/distro), /work and
# /build are not used: the suite runs from /src and tests that binary.
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
    # Clients (krema, fixtures) render with llvmpipe, like kwin --virtual.
    export LIBGL_ALWAYS_SOFTWARE=1
    cd "$KREMA_E2E_TESTS_DIR"
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

mkdir -p /artifacts
if [ -n "${KREMA_E2E_BINARY:-}" ]; then
    [ -x "$KREMA_E2E_BINARY" ] || { echo "KREMA_E2E_BINARY=$KREMA_E2E_BINARY is not an executable" >&2; exit 1; }
    echo "[e2e] krema under test: $KREMA_E2E_BINARY${KREMA_E2E_DISTRO:+ ($KREMA_E2E_DISTRO)}"
    # /src is read-only.
    export PYTHONDONTWRITEBYTECODE=1
    export KREMA_E2E_TESTS_DIR=/src/tests/appium
else
    mkdir -p /work
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
    export KREMA_E2E_TESTS_DIR=/work/tests/appium
fi
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
# KWin always runs `--virtual` (what run.rb passes when LIBGL_ALWAYS_SOFTWARE
# is set). It composites with OpenGL only on a device with a render node
# (vgem, tests/appium/setup-vgem.sh); otherwise it falls back to QPainter (no
# screenshots, no screencast).
export LIBGL_ALWAYS_SOFTWARE=1
# Opt-in multi-output session (tools/run-output-count.patch): outputs of
# SCREEN_WIDTH x SCREEN_HEIGHT each, placed side by side left to right.
export KREMA_E2E_OUTPUT_COUNT="${KREMA_E2E_OUTPUT_COUNT:-1}"
if [ "$KREMA_E2E_OUTPUT_COUNT" -gt 1 ]; then
    export COMPOSITOR_OUTPUT_COUNT="$KREMA_E2E_OUTPUT_COUNT"
fi
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
# Tier 3 installs krema from its distro package, so /usr/bin/krema's desktop
# file is present and declares X-KDE-Wayland-Interfaces. Keep KWin's
# permission checks enabled (run-permission-checks.patch makes the hard-coded
# bypass overridable): KWin then honours the declaration and grants the
# client privileged xdg-activation, like on a real session. A source-tree
# krema has no matching desktop file and must keep the bypass.
if [ -n "${KREMA_E2E_DISTRO:-}" ]; then
    export KWIN_WAYLAND_NO_PERMISSION_CHECKS=0
fi
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
