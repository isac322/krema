#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Launch the wire-level notification QA app in the current Plasma/KWin session.
# Optional --capture records the same Wayland session with wf-recorder.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$here/../.." && pwd)
app="$repo/tools/notification-debugger.py"
jsonl=
scenario=
capture=


usage() {
    cat <<EOF
Usage: $0 [OPTIONS] [-- APP-ARG ...]

Options:
  --jsonl PATH       JSONL event output path
  --scenario ID      scenario ID from tests/notification/scenarios.json
  --capture PATH      record the current Wayland session with wf-recorder
  -h, --help          show this help

Arguments after -- are passed unchanged to notification-debugger.py.
EOF
}

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

[ -f "$app" ] || { echo "QA app not found: $app" >&2; exit 1; }

if [ -n "$capture" ]; then
    command -v wf-recorder >/dev/null 2>&1 || {
        echo "--capture requires wf-recorder in PATH" >&2
        exit 1
    }
    case "${XDG_SESSION_TYPE:-wayland}" in
        wayland) ;;
        *)
            echo "--capture requires a Wayland Plasma/KWin session" >&2
            exit 1
            ;;
    esac
fi

run_app() {
    if [ -n "$jsonl" ] && [ -n "$scenario" ]; then
        python3 "$app" --jsonl "$jsonl" --scenario "$scenario" "$@"
    elif [ -n "$jsonl" ]; then
        python3 "$app" --jsonl "$jsonl" "$@"
    elif [ -n "$scenario" ]; then
        python3 "$app" --scenario "$scenario" "$@"
    else
        python3 "$app" "$@"
    fi
}

app_pid=
recorder_pid=
recorder_status=0
cleanup() {
    if [ -n "$app_pid" ] && kill -0 "$app_pid" 2>/dev/null; then
        kill "$app_pid" 2>/dev/null || true
    fi
    if [ -n "$recorder_pid" ] && kill -0 "$recorder_pid" 2>/dev/null; then
        kill -INT "$recorder_pid" 2>/dev/null || true
        sleep 1
        kill "$recorder_pid" 2>/dev/null || true
    fi
}
trap cleanup INT TERM EXIT

if [ -n "$capture" ]; then
    echo "[notification-qa] video capture: $capture"
    wf-recorder -f "$capture" &
    recorder_pid=$!
fi

echo "[notification-qa] app: $app"
[ -n "$scenario" ] && echo "[notification-qa] scenario: $scenario"
[ -n "$jsonl" ] && echo "[notification-qa] JSONL: $jsonl"

run_app "$@" &
app_pid=$!
set +e
wait "$app_pid"
status=$?
set -e
app_pid=

if [ -n "$recorder_pid" ]; then
    kill -INT "$recorder_pid" 2>/dev/null || true
    sleep 1
    kill "$recorder_pid" 2>/dev/null || true
    set +e
    wait "$recorder_pid" 2>/dev/null
    recorder_status=$?
    set -e
    recorder_pid=
fi

trap - INT TERM EXIT
if [ "$status" -eq 0 ] && [ "$recorder_status" -ne 0 ]; then
    exit "$recorder_status"
fi
exit "$status"
