#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Run the notification QA GUI in the appium image's real Wayland session.
# The outer phase builds source Krema; selenium-webdriver-at-spi-run then
# supplies one private D-Bus session, KWin, and AT-SPI environment.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
script="$here/container-session.sh"
repo=$(CDPATH= cd -- "$here/../.." && pwd)
app="$repo/tools/notification-debugger.py"
recorder="$here/screenshot-recorder.py"
artifacts=${KREMA_NOTIFICATION_ARTIFACTS:-/artifacts}
binary=${KREMA_NOTIFICATION_BINARY:-}

usage() {
    cat <<EOF
Usage: $0 [OPTIONS] [-- APP-ARG ...]

Runs a source-built Krema and the notification debugger in one real
kwin_wayland/plasmashell/D-Bus session inside the appium container.

Options:
  --jsonl PATH       JSONL event output path (default: /artifacts/notification-qa.jsonl)
  --scenario ID      scenario ID from tests/notification/scenarios.json
  --capture PATH     record the Wayland session with KWin ScreenShot2
                     (requires OpenGL/DRM capture support)

Arguments after -- are passed unchanged to notification-debugger.py.
EOF
}

wait_for_name() {
    name=$1
    tries=0
    while ! dbus-send --session --print-reply=literal \
        --dest=org.freedesktop.DBus / org.freedesktop.DBus.NameHasOwner \
        string:"$name" 2>/dev/null | grep -q true; do
        tries=$((tries + 1))
        [ "$tries" -le 200 ] || {
            echo "D-Bus service did not start: $name" >&2
            return 1
        }
        sleep 0.05
    done
}

if [ "${1:-}" != "--inner" ]; then
    case "${1:-}" in
        -h|--help)
            usage
            exit 0
            ;;
    esac

    [ -f "$repo/CMakeLists.txt" ] || {
        echo "Krema source checkout not found at $repo" >&2
        exit 1
    }
    mkdir -p "$artifacts" /build

    if [ -z "$binary" ]; then
        if [ ! -f /build/build.ninja ]; then
            cmake -S "$repo" -B /build -G Ninja \
                -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                -DBUILD_TESTING=OFF
        fi
        cmake --build /build --target krema
        binary=$(find /build -type f -name krema -perm -u+x -path '*/bin/*' | head -n 1)
    fi
    [ -x "$binary" ] || {
        echo "Krema binary not found or not executable: ${binary:-<unset>}" >&2
        exit 1
    }

    export KREMA_NOTIFICATION_BINARY=$binary
    export KREMA_NOTIFICATION_ARTIFACTS=$artifacts
    export XDG_RUNTIME_DIR=/tmp/runtime-notification-qa
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 700 "$XDG_RUNTIME_DIR"
    export APPIUM_ARTIFACT_OUTPUT_PATH=$artifacts
    export USE_CUSTOM_BUS=1
    export LIBGL_ALWAYS_SOFTWARE=1
    export KREMA_E2E_OUTPUT_COUNT="${KREMA_E2E_OUTPUT_COUNT:-1}"
    if [ "$KREMA_E2E_OUTPUT_COUNT" -gt 1 ]; then
        export COMPOSITOR_OUTPUT_COUNT="$KREMA_E2E_OUTPUT_COUNT"
    fi
    export COMPOSITOR_WIDTH="${KREMA_E2E_SCREEN_WIDTH:-1024}"
    export COMPOSITOR_HEIGHT="${KREMA_E2E_SCREEN_HEIGHT:-768}"
    export KREMA_E2E_SCREEN_WIDTH="$COMPOSITOR_WIDTH"
    export KREMA_E2E_SCREEN_HEIGHT="$COMPOSITOR_HEIGHT"
    export TEST_WITHOUT_GLOBAL_SHORTCUTS=0
    export TEST_WITH_VIDEO_RECORDER=0
    export QT_FORCE_STDERR_LOGGING=1
    export KWIN_WAYLAND_NO_PERMISSION_CHECKS=1
    export QT_QPA_PLATFORM=wayland
    export XDG_CURRENT_DESKTOP=KDE
    export XDG_SESSION_TYPE=wayland
    export KDE_FULL_SESSION=true

    exec selenium-webdriver-at-spi-run sh "$script" --inner "$@"
fi
shift

jsonl="$artifacts/notification-qa.jsonl"
scenario=manual
capture=

while [ "$#" -gt 0 ]; do
    case "$1" in
        --jsonl)
            [ "$#" -ge 2 ] || { echo "--jsonl requires PATH" >&2; exit 2; }
            jsonl=$2
            shift 2
            ;;
        --scenario)
            [ "$#" -ge 2 ] || { echo "--scenario requires ID" >&2; exit 2; }
            scenario=$2
            shift 2
            ;;
        --capture)
            [ "$#" -ge 2 ] || { echo "--capture requires PATH" >&2; exit 2; }
            capture=$2
            shift 2
            ;;
        --)
            shift
            break
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown launcher option: $1 (put app arguments after --)" >&2
            usage >&2
            exit 2
            ;;
    esac
done

[ -f "$recorder" ] || { echo "ScreenShot2 recorder not found: $recorder" >&2; exit 1; }
[ -f "$app" ] || { echo "QA app not found: $app" >&2; exit 1; }
[ -x "$KREMA_NOTIFICATION_BINARY" ] || {
    echo "KREMA_NOTIFICATION_BINARY is not executable: $KREMA_NOTIFICATION_BINARY" >&2
    exit 1
}

if [ -n "$capture" ]; then
    command -v selenium-webdriver-at-spi-screenshotter >/dev/null 2>&1 || {
        echo "--capture requires selenium-webdriver-at-spi-screenshotter in PATH" >&2
        exit 1
    }
    command -v ffmpeg >/dev/null 2>&1 || {
        echo "--capture requires ffmpeg in PATH" >&2
        exit 1
    }
    [ "${XDG_SESSION_TYPE:-wayland}" = wayland ] || {
        echo "--capture requires a Wayland session" >&2
        exit 1
    }
fi

mkdir -p "$(dirname "$jsonl")"
[ -z "$capture" ] || mkdir -p "$(dirname "$capture")"
stop_file=
if [ -n "$capture" ]; then
    stop_file="$capture.stop.$$"
    : >"$stop_file"
fi

stop_group() {
    pid=$1
    [ -n "$pid" ] || return 0
    kill -TERM -- "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
}

plasmashell_pid=
portal_pid=
portal_kde_pid=
krema_pid=
recorder_pid=
recorder_status=0
app_pid=
cleanup() {
    [ -z "$stop_file" ] || rm -f "$stop_file"
    for pid in "$app_pid" "$recorder_pid" "$krema_pid" "$portal_kde_pid" "$portal_pid" "$plasmashell_pid"; do
        stop_group "$pid"
    done
    sleep 1
    for pid in "$app_pid" "$recorder_pid" "$krema_pid" "$portal_kde_pid" "$portal_pid" "$plasmashell_pid"; do
        [ -n "$pid" ] || continue
        kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
    done
}

trap cleanup INT TERM EXIT

setsid plasmashell --no-respawn >"$artifacts/plasmashell.log" 2>&1 &
plasmashell_pid=$!
wait_for_name org.kde.plasmashell
wait_for_name org.freedesktop.Notifications
wait_for_name org.kde.StatusNotifierWatcher

# Use the installed KDE portal backend, not a mock notification service. The
# portal is optional for non-portal scenarios, but starting it makes the
# Portal tab exercise the same session path as a Plasma desktop.
if command -v xdg-desktop-portal-kde >/dev/null 2>&1; then
    xdg-desktop-portal-kde >"$artifacts/xdg-desktop-portal-kde.log" 2>&1 &
    portal_kde_pid=$!
else
    echo "[notification-qa] xdg-desktop-portal-kde not found; backend will be D-Bus activated"
fi
xdg-desktop-portal >"$artifacts/xdg-desktop-portal.log" 2>&1 &
portal_pid=$!

setsid "$KREMA_NOTIFICATION_BINARY" >"$artifacts/krema.log" 2>&1 &
krema_pid=$!
sleep 1
kill -0 "$krema_pid" 2>/dev/null || {
    echo "Krema exited during session startup; see $artifacts/krema.log" >&2
    exit 1
}

if [ -n "$capture" ]; then
    echo "[notification-qa] video capture: $capture"
    setsid python3 "$recorder" "$capture" "$stop_file" >"$artifacts/screenshot-recorder.log" 2>&1 &
    recorder_pid=$!
fi

echo "[notification-qa] session: real kwin_wayland/plasmashell via selenium-webdriver-at-spi-run"
echo "[notification-qa] Krema: $KREMA_NOTIFICATION_BINARY"
echo "[notification-qa] scenario: $scenario"
echo "[notification-qa] JSONL: $jsonl"
[ -n "$capture" ] && echo "[notification-qa] capture: $capture"

if [ -n "$scenario" ]; then
    python3 "$app" --jsonl "$jsonl" --scenario "$scenario" "$@" &
else
    python3 "$app" --jsonl "$jsonl" "$@" &
fi
app_pid=$!
set +e
wait "$app_pid"
status=$?
set -e
app_pid=

if [ -n "$recorder_pid" ]; then
    rm -f "$stop_file"
    stop_group "$recorder_pid"
    if wait "$recorder_pid" 2>/dev/null; then
        recorder_status=0
    else
        recorder_status=$?
    fi
    recorder_pid=
    stop_file=
fi
if [ "$status" -eq 0 ] && [ "$recorder_status" -ne 0 ]; then
    status=$recorder_status
fi

trap - INT TERM EXIT
cleanup
exit "$status"
