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
unset WAYLAND_DISPLAY DISPLAY QT_QPA_PLATFORM
# Tests find controls by their English labels.
export LANGUAGE=C LC_ALL=C.UTF-8
# Keep Qt warnings in the test output instead of the journal.
export QT_FORCE_STDERR_LOGGING=1

socket=krema-test
kwin_wayland --virtual --no-lockscreen --socket "$socket" --width 1024 --height 768 --output-count 2 >"$scratch/kwin.log" 2>&1 &
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
