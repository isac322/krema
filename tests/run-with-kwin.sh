#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Runs a GUI integration test against a private KWin virtual compositor with
# two outputs, a private session bus and throwaway XDG directories, so the test
# never touches the user's session, config or running dock.
#
# Usage: run-with-kwin.sh <test-binary> [args...]

set -eu

scratch=$(mktemp -d)
kwin_pid=
# Must not fail: under set -e a failing kill/wait (KWin already gone) would
# end the trap before the scratch directory is removed.
cleanup() {
    if [ -n "$kwin_pid" ]; then
        kill "$kwin_pid" 2>/dev/null || true
        wait "$kwin_pid" 2>/dev/null || true
    fi
    rm -rf "$scratch"
}
trap cleanup EXIT
trap 'exit 1' INT TERM

export XDG_RUNTIME_DIR="$scratch/runtime"
export XDG_CONFIG_HOME="$scratch/config"
export XDG_DATA_HOME="$scratch/data"
export XDG_CACHE_HOME="$scratch/cache"
mkdir -p "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
chmod 700 "$XDG_RUNTIME_DIR"
unset WAYLAND_DISPLAY DISPLAY QT_QPA_PLATFORM QT_WAYLAND_SHELL_INTEGRATION
# Tests find controls by their English labels.
export LANGUAGE=C LC_ALL=C.UTF-8
# Keep Qt warnings in the test output instead of the journal.
export QT_FORCE_STDERR_LOGGING=1
# Let the test bind KWin-restricted protocols (plasma-window-management etc.)
# without a registered desktop file.
export KWIN_WAYLAND_NO_PERMISSION_CHECKS=1

socket=krema-test
# Optional test-only KWin effects (a directory of effect packages), installed
# into the private data directory before KWin starts. KWin only loads scripted
# effects when animations are supported, which the virtual backend's software
# compositing does not report unless forced.
if [ -n "${KREMA_TEST_KWIN_EFFECTS:-}" ]; then
    mkdir -p "$XDG_DATA_HOME/kwin/effects"
    cp -R "$KREMA_TEST_KWIN_EFFECTS"/. "$XDG_DATA_HOME/kwin/effects/"
    export KWIN_EFFECTS_FORCE_ANIMATIONS=1
fi
# Tests may read what KWin scripts and effects log (console.* -> "js").
export KREMA_TEST_KWIN_LOG="$scratch/kwin.log"
QT_LOGGING_RULES="js.info=true" kwin_wayland --virtual --no-lockscreen --socket "$socket" --width 1024 --height 768 --output-count 2 >"$KREMA_TEST_KWIN_LOG" 2>&1 &
kwin_pid=$!

tries=0
while [ ! -S "$XDG_RUNTIME_DIR/$socket" ]; do
    tries=$((tries + 1))
    if [ "$tries" -gt 200 ] || ! kill -0 "$kwin_pid" 2>/dev/null; then
        echo "kwin_wayland did not start:" >&2
        cat "$scratch/kwin.log" >&2
        exit 1
    fi
    sleep 0.1
done

WAYLAND_DISPLAY="$socket" QT_QPA_PLATFORM=wayland "$@"
