#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Entry point for tests/ci/Dockerfile. Builds krema with the frame probe, starts
# an in-container `kwin_wayland --virtual` compositor, replays every scenario in
# tests/ci/scenarios/ twice and asserts the captured frames.
#
# Usage: run-frame-tests.sh [SCENARIO_NAME ...]   (default: every scenario)
#
# Environment:
#   KREMA_SRC       source checkout (default /src)
#   KREMA_OUT       artifact directory (default /out)
#   KREMA_WIDTH     virtual output width (default 1024)
#   KREMA_HEIGHT    virtual output height (default 768)
#   KREMA_PASSES    capture passes per scenario; >=2 enables the determinism
#                   comparison (default 2)
#   KREMA_SCREENSHOTS  1 = dump a PNG per frame and emit a review manifest for
#                   visual/LLM review (default 0; PNG capture is the slow part)
#   KREMA_VIDEO     1 = also encode <scenario>.webm from those frames, annotated
#                   with frame numbers, actions and assertion anchors. Implies
#                   KREMA_SCREENSHOTS=1.
#   KREMA_VIDEO_FPS playback fps for the video (default 30, about half speed)
#   KREMA_PREVIEW_BASE_URL  raw URL prefix the published GIFs will live at; the
#                   generated comment.md embeds them from there
#   KREMA_SCENARIO_DIR  directory of scenario JSON to run (default
#                   tests/ci/scenarios); used by vacuity_check.py to run
#                   stimulus-free controls
#   KREMA_KEEP_FRAMES  1 = keep every raw PNG. Default 0 prunes them after
#                   encoding, keeping only the keyframes review.md links.

set -uo pipefail

export KREMA_SRC="${KREMA_SRC:-/src}"
export KREMA_OUT="${KREMA_OUT:-/out}"
export KREMA_WIDTH="${KREMA_WIDTH:-1024}"
export KREMA_HEIGHT="${KREMA_HEIGHT:-768}"
export KREMA_PASSES="${KREMA_PASSES:-2}"
export KREMA_VIDEO="${KREMA_VIDEO:-0}"
export KREMA_VIDEO_FPS="${KREMA_VIDEO_FPS:-30}"
export KREMA_KEEP_FRAMES="${KREMA_KEEP_FRAMES:-0}"
export KREMA_SCREENSHOTS="${KREMA_SCREENSHOTS:-0}"
if [[ "$KREMA_VIDEO" == "1" ]]; then
    KREMA_SCREENSHOTS=1
fi
build=/tmp/krema-build

export XDG_RUNTIME_DIR=/tmp/krema-runtime
export KWIN_WAYLAND_NO_PERMISSION_CHECKS=1
export KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1
export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe
export QT_FORCE_STDERR_LOGGING=1
# KService and the icon loader both key off this; it is unset in the image.
export XDG_DATA_DIRS="${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
mkdir -p "$XDG_RUNTIME_DIR" "$KREMA_OUT"
chmod 700 "$XDG_RUNTIME_DIR"

# The compositor must not inherit a client-side Wayland/QPA environment or it
# tries to run nested instead of using its own virtual backend.
unset WAYLAND_DISPLAY QT_QPA_PLATFORM

scenario_dir="${KREMA_SCENARIO_DIR:-$KREMA_SRC/tests/ci/scenarios}"
if (($# > 0)); then
    selected=()
    for name in "$@"; do selected+=("$scenario_dir/$name.json"); done
else
    mapfile -t selected < <(find "$scenario_dir" -name '*.json' | sort)
fi
export KREMA_SCENARIOS="${selected[*]}"

# ECM's kde_configure_git_pre_commit_hook writes into .git/hooks, and the
# checkout is mounted read-only, so configure aborts on "Read-only file system".
# Build from a copy without .git: ECM then skips the hook entirely and nothing
# in the build can reach back into the mounted tree.
echo "== stage source =="
src_copy=/tmp/krema-src
rm -rf "$src_copy"
mkdir -p "$src_copy"
tar -c -C "$KREMA_SRC" --exclude=.git --exclude=build . | tar -x -C "$src_copy"
KREMA_SRC_BUILD="$src_copy"

echo "== configure =="
cmake -S "$KREMA_SRC_BUILD" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DKREMA_TEST_HOOKS=ON \
    -DCMAKE_INSTALL_PREFIX=/usr >"$KREMA_OUT/cmake-configure.log" 2>&1 || {
    echo "configure failed" >&2
    # The feature summary CMake prints on failure is ~40 lines by itself, so a
    # short tail hides the actual error. Show the error lines first, then more
    # context than the summary occupies.
    grep -nE "CMake Error|Could NOT find|CMAKE_[A-Z_]*NOTFOUND" \
        "$KREMA_OUT/cmake-configure.log" >&2 || true
    tail -n 120 "$KREMA_OUT/cmake-configure.log" >&2
    exit 1
}

echo "== build =="
cmake --build "$build" -j"$(nproc)" >"$KREMA_OUT/cmake-build.log" 2>&1 || {
    echo "build failed" >&2
    grep -nE "error:|Error [0-9]|FAILED:" "$KREMA_OUT/cmake-build.log" | head -n 40 >&2
    tail -n 40 "$KREMA_OUT/cmake-build.log" >&2
    exit 1
}
cmake --install "$build" >"$KREMA_OUT/cmake-install.log" 2>&1

echo "== build window fixture =="
cmake -S "$KREMA_SRC/tests/ci/fixture-client" -B /tmp/fixture-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release >"$KREMA_OUT/fixture-configure.log" 2>&1 &&
    cmake --build /tmp/fixture-build >"$KREMA_OUT/fixture-build.log" 2>&1 || {
    echo "fixture build failed" >&2
    tail -n 20 "$KREMA_OUT/fixture-build.log" >&2
    exit 1
}
export KREMA_FIXTURE_BIN=/tmp/fixture-build/krema-fixture-window

session() {
    local socket=krema-ci
    kwin_wayland --virtual --no-lockscreen --socket "$socket" \
        --width "$KREMA_WIDTH" --height "$KREMA_HEIGHT" \
        >"$KREMA_OUT/kwin.log" 2>&1 &
    local kwin_pid=$!

    local waited=0
    while ((waited < 80)) && [[ ! -S "$XDG_RUNTIME_DIR/$socket" ]]; do
        sleep 0.25
        ((waited++))
    done
    if [[ ! -S "$XDG_RUNTIME_DIR/$socket" ]]; then
        echo "kwin_wayland did not create $XDG_RUNTIME_DIR/$socket" >&2
        tail -n 40 "$KREMA_OUT/kwin.log" >&2
        return 1
    fi
    sleep 2

    local failures=0
    local scenario
    for scenario in $KREMA_SCENARIOS; do
        local name
        name="$(basename "$scenario" .json)"
        echo "== scenario $name =="

        # Scenarios that exercise previews, running indicators or active state
        # need real toplevel windows: TasksModel only grows window rows when
        # something maps a toplevel surface on this compositor.
        local wanted
        wanted="$(python3 -c "
import json,sys
data = json.load(open(sys.argv[1]))
print(data.get('fixture_windows', 0) if isinstance(data, dict) else 0)
" "$scenario")"
        local fixture_pids=()
        local n
        for ((n = 0; n < wanted; n++)); do
            env WAYLAND_DISPLAY="$socket" QT_QPA_PLATFORM=wayland \
                "$KREMA_FIXTURE_BIN" "Fixture $n" "org.kde.kcalc" \
                >"$KREMA_OUT/fixture-$name-$n.log" 2>&1 &
            fixture_pids+=($!)
        done
        if ((wanted > 0)); then
            sleep 4
            echo "   launched $wanted fixture window(s)"
        fi

        local captured=1
        local pass
        for ((pass = 1; pass <= KREMA_PASSES; pass++)); do
            local dir="$KREMA_OUT/$name/pass$pass"
            rm -rf "$dir" /tmp/krema-home
            mkdir -p "$dir/frames" /tmp/krema-home/.config
            # Pristine HOME per pass: identical pinned launchers and geometry.
            # A scenario's "config" block becomes kremarc, which is how a user's
            # saved settings reach the dock at startup. krema has no
            # KConfigWatcher, so runtime changes must use the "setting" action
            # instead of rewriting this file.
            python3 -c "
import json, sys, collections
data = json.load(open(sys.argv[1]))
config = data.get('config', {}) if isinstance(data, dict) else {}
# Pin launchers that actually exist in this image. krema's shipped default
# points at Dolphin, Konsole, Kate and System Settings, none of which are
# installed here, so every item would render as a generic placeholder.
# Absolute file URLs, not applications: ids. LauncherTasksModel builds the
# KService straight from the .desktop path, so nothing depends on a ksycoca
# database being present and keyed to a matching XDG_DATA_DIRS hash.
config = {'General/PinnedLaunchers': ','.join(
    'file:///usr/share/applications/org.kde.%s.desktop' % app
    for app in ('kwrite', 'kfind', 'okular', 'gwenview')), **config}
if config:
    groups = collections.defaultdict(dict)
    for key, value in config.items():
        group, _, entry = key.rpartition('/')
        if isinstance(value, bool):
            value = 'true' if value else 'false'
        groups[group or 'General'][entry] = value
    with open(sys.argv[2], 'w') as out:
        for group, entries in groups.items():
            out.write('[%s]\\n' % group)
            for entry, value in entries.items():
                out.write('%s=%s\\n' % (entry, value))
            out.write('\\n')
" "$scenario" /tmp/krema-home/.config/kremarc

            # A scenario may need a longer capture than the default: the edge
            # transition alone takes ~45 frames to settle.
            local max_frames
            max_frames="$(python3 -c "
import json, sys
data = json.load(open(sys.argv[1]))
print(data.get('max_frames', 0) if isinstance(data, dict) else 0)
" "$scenario")"
            if [[ "$max_frames" == "0" ]]; then
                max_frames="${KREMA_PROBE_MAX_FRAMES:-140}"
            fi

            local probe_dir=""
            if [[ "$KREMA_SCREENSHOTS" == "1" ]]; then
                probe_dir="$dir/frames"
            fi
            env HOME=/tmp/krema-home \
                XDG_DATA_DIRS="$XDG_DATA_DIRS" \
                WAYLAND_DISPLAY="$socket" \
                QT_QPA_PLATFORM=wayland \
                KREMA_PROBE_NDJSON="$dir/frames.ndjson" \
                KREMA_PROBE_DIR="$probe_dir" \
                KREMA_PROBE_SCRIPT="$scenario" \
                KREMA_PROBE_STEP_MS="${KREMA_PROBE_STEP_MS:-16}" \
                KREMA_PROBE_SETTLE_FRAMES="${KREMA_PROBE_SETTLE_FRAMES:-40}" \
                KREMA_PROBE_MAX_FRAMES="$max_frames" \
                timeout 180 krema >"$dir/krema.log" 2>&1
            local status=$?
            local rows
            rows="$(wc -l <"$dir/frames.ndjson" 2>/dev/null || echo 0)"
            echo "   pass$pass exit=$status frames=$rows"
            if ((status != 0)) || ((rows == 0)); then
                tail -n 20 "$dir/krema.log" >&2
                captured=0
                break
            fi
        done

        local pid
        for pid in "${fixture_pids[@]:-}"; do
            [[ -n "$pid" ]] && kill "$pid" 2>/dev/null
        done

        if ((captured == 0)); then
            ((failures++))
            continue
        fi
        local review=()
        if [[ "$KREMA_SCREENSHOTS" == "1" ]]; then
            review=(--review)
        fi
        if [[ "$KREMA_VIDEO" == "1" ]]; then
            python3 "$KREMA_SRC/tests/ci/make_video.py" \
                --scenario "$scenario" \
                --run-dir "$KREMA_OUT/$name" \
                --fps "$KREMA_VIDEO_FPS" || true
        fi

        if ! python3 "$KREMA_SRC/tests/ci/assert_frames.py" \
            --scenario "$scenario" \
            --run-dir "$KREMA_OUT/$name" \
            "${review[@]}"; then
            ((failures++))
        fi

        # After review.json exists, drop every frame it does not reference.
        if [[ "$KREMA_SCREENSHOTS" == "1" && "$KREMA_KEEP_FRAMES" != "1" ]]; then
            python3 "$KREMA_SRC/tests/ci/prune_frames.py" --run-dir "$KREMA_OUT/$name" || true
        fi
    done

    kill "$kwin_pid" 2>/dev/null

    # Animations for the pull request comment: only what someone would open.
    if [[ "$KREMA_VIDEO" == "1" ]]; then
        python3 "$KREMA_SRC/tests/ci/make_previews.py" \
            --run-dir "$KREMA_OUT" \
            --out "$KREMA_OUT/previews" \
            --base-url "${KREMA_PREVIEW_BASE_URL:-}" || true
    fi

    if ((failures > 0)); then
        echo "$failures scenario(s) failed" >&2
        return 1
    fi
    echo "all scenarios passed"
}
export -f session

dbus-run-session -- bash -c session
