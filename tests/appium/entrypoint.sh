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
    # Proof that the session got as far as pytest: a KWin crash before this
    # point cannot have affected any test (see the startup retry below).
    : >"$KREMA_E2E_STATE/inner-started"
    # Clients (krema, fixtures) render with llvmpipe, like kwin --virtual.
    export LIBGL_ALWAYS_SOFTWARE=1
    # Like Plasma, which starts kactivitymanagerd as a session service
    # (plasma-kactivitymanagerd.service, with the session's environment), run
    # it inside the kwin session when it is installed (Tier 3: krema's
    # plasma-workspace dependency pulls it in). Otherwise krema's
    # TaskManager::ActivityInfo D-Bus-activates it on every start from the
    # bus's environment, which has no WAYLAND_DISPLAY: it tries xcb and
    # aborts (SIGABRT), once per krema start, each crash handed to the host's
    # core handler (apport on the CI runners).
    for d in $(echo "${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" | tr ':' ' '); do
        f="$d/dbus-1/services/org.kde.ActivityManager.service"
        [ -f "$f" ] || continue
        exec_line=$(sed -n 's/^Exec=//p' "$f")
        # shellcheck disable=SC2086  # Exec= is a command line; word-split it
        $exec_line >>/artifacts/kactivitymanagerd.log 2>&1 &
        tries=0
        until dbus-send --session --print-reply=literal --dest=org.freedesktop.DBus / \
            org.freedesktop.DBus.NameHasOwner string:org.kde.ActivityManager 2>/dev/null | grep -q true; do
            tries=$((tries + 1))
            [ "$tries" -le 100 ] || { echo "kactivitymanagerd did not start" >&2; cat /artifacts/kactivitymanagerd.log >&2; exit 1; }
            sleep 0.05
        done
        break
    done
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
# interface (used by KPipeWire previews) never connects. Started per session
# attempt, in that attempt's XDG_RUNTIME_DIR.
start_pipewire() {
    pipewire >>/artifacts/pipewire.log 2>&1 &
    tries=0
    until [ -S "$XDG_RUNTIME_DIR/pipewire-0" ]; do
        tries=$((tries + 1))
        [ "$tries" -le 100 ] || { echo "pipewire did not start" >&2; cat /artifacts/pipewire.log >&2; exit 1; }
        sleep 0.05
    done
    wireplumber >>/artifacts/wireplumber.log 2>&1 &
}

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

# run.rb only aborts when the nested kwin_wayland ends, without saying how,
# and a crashing KWin may print nothing at all (seen on openSUSE). This shim
# (first on run.rb's PATH) runs the real binary, reports its exit status or
# signal on stderr (session.log) and records a signal death in the session
# attempt's state dir for the startup retry below.
kwin_bin=$(command -v kwin_wayland)
mkdir -p /tmp/krema-e2e-bin
cat >/tmp/krema-e2e-bin/kwin_wayland <<EOF
#!/bin/sh
$kwin_bin "\$@"
rc=\$?
if [ "\$rc" -gt 128 ]; then
    echo "[e2e] kwin_wayland killed by signal \$((rc - 128))" >&2
    echo "\$((rc - 128))" >"\$KREMA_E2E_STATE/kwin-signal"
else
    echo "[e2e] kwin_wayland exited with status \$rc" >&2
fi
exit "\$rc"
EOF
chmod +x /tmp/krema-e2e-bin/kwin_wayland
export PATH="/tmp/krema-e2e-bin:$PATH"

# Best effort, for upstream reports: when the host's core_pattern is a path
# visible in the container (e.g. core_pattern=/tmp/cores/core.%e.%p with
# KREMA_E2E_DOCKER_ARGS="-v /tmp/cores:/tmp/cores --ulimit core=-1") and the
# image has gdb, write a backtrace of each core dumped during the current
# attempt to /artifacts/<$1>backtrace-<core>.txt. %e is the crashing thread's
# name (KWin's startup crash dumps core.QQmlThread.*), so cores are matched
# by time and read against kwin_wayland.
kwin_core_backtraces() {
    pattern=$(cat /proc/sys/kernel/core_pattern 2>/dev/null) || return 0
    case "$pattern" in /*) ;; *) return 0 ;; esac
    if ! command -v gdb >/dev/null 2>&1; then
        echo "[e2e] no gdb in the image: cores under $(dirname "$pattern") not analysed"
        return 0
    fi
    prefix=$(basename "$pattern")
    prefix=${prefix%%%*}
    find "$(dirname "$pattern")" -maxdepth 1 -type f -name "$prefix*" -newer "$KREMA_E2E_STATE/started" 2>/dev/null |
        while read -r core; do
            out="/artifacts/$1backtrace-$(basename "$core").txt"
            timeout 600 gdb -q -batch -ex 'set debuginfod enabled on' -ex 'set pagination off' \
                -ex 'info threads' -ex 'thread apply all bt 40' "$kwin_bin" "$core" >"$out" 2>&1 || true
            echo "[e2e] kwin_wayland core backtrace: $out"
        done
}

# One session attempt ($1: attempt number, then the pytest arguments) with
# its own state dir, XDG_RUNTIME_DIR and PipeWire; run.rb creates fresh XDG
# config/data/cache/state homes itself. Sets rc.
run_session() {
    export KREMA_E2E_STATE="/tmp/krema-e2e-state-$1"
    export XDG_RUNTIME_DIR="/tmp/runtime-root-$1"
    shift
    mkdir -p "$KREMA_E2E_STATE" "$XDG_RUNTIME_DIR"
    chmod 700 "$XDG_RUNTIME_DIR"
    : >"$KREMA_E2E_STATE/started"
    start_pipewire
    : >/artifacts/pytest.log
    selenium-webdriver-at-spi-run sh /src/tests/appium/entrypoint.sh --inner "$@" >/artifacts/session.log 2>&1 &
    session_pid=$!
    # Stream pytest output live; --pid makes tail drain the file and exit
    # once the session is gone.
    tail -n +1 -s 0.2 -F --pid="$session_pid" /artifacts/pytest.log 2>/dev/null || true
    set +e
    wait "$session_pid"
    rc=$?
    set -e
}

t0=$(stamp)
run_session 1 "$@"
# KWin sometimes dies from SIGSEGV (in its QQmlThread) while the session is
# still starting, before run.rb hands over to pytest: an upstream crash that
# no test can have caused or observed. Only that case restarts the whole
# session, once: the session failed, KWin died from a signal, and the inner
# half never started (no pytest, no krema). A crash after pytest started, a
# second startup crash, or any other failure fails as before.
if [ "$rc" -ne 0 ] && [ -s "$KREMA_E2E_STATE/kwin-signal" ] && [ ! -e "$KREMA_E2E_STATE/inner-started" ]; then
    echo "[e2e] kwin_wayland crashed during session startup (signal $(cat "$KREMA_E2E_STATE/kwin-signal")); restarting the session once"
    grep -E '^KCrash:|^\[e2e\] kwin_wayland |Segmentation fault|core dumped' /artifacts/session.log || true
    kwin_core_backtraces startup-crash-
    # run.rb rewrites these logs: keep the crashed attempt's.
    mkdir -p /artifacts/startup-crash
    mv /artifacts/session.log /artifacts/appium_artifact_* /artifacts/startup-crash/ 2>/dev/null || true
    # Nothing of the crashed attempt may leak into the next one (webdriver
    # port, D-Bus, AT-SPI, PipeWire): kill every process but init and this
    # shell.
    for pid in $(ps -eo pid=); do
        [ "$pid" -eq 1 ] || [ "$pid" -eq $$ ] || kill -KILL "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    run_session 2 "$@"
fi
if [ "$rc" -ne 0 ]; then
    grep '^\[e2e\] kwin_wayland ' /artifacts/session.log || true
    [ ! -s "$KREMA_E2E_STATE/kwin-signal" ] || kwin_core_backtraces ""
fi
echo "[e2e] tests: $(elapsed "$t0")s (exit $rc)"
exit "$rc"
