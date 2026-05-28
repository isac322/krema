#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
targets_file="$script_dir/targets.tsv"
image_prefix="${KREMA_DOCKER_IMAGE_PREFIX:-krema/gui-runtime}"
docker_host="${DOCKER_HOST:-tcp://localhost:2375}"
screen_width="${KREMA_SMOKE_WIDTH:-800}"
screen_height="${KREMA_SMOKE_HEIGHT:-600}"
smoke_timeout="${KREMA_SMOKE_TIMEOUT:-10}"
smoke_settle_seconds="${KREMA_SMOKE_SETTLE_SECONDS:-4}"
smoke_ready_timeout="${KREMA_SMOKE_READY_TIMEOUT:-180}"
screenshot_dir="${KREMA_SMOKE_SCREENSHOT_DIR:-}"

usage() {
    cat <<'EOF'
Usage: tests/docker/run-smoke.sh TARGET_ID PACKAGE_DIR

TARGET_ID must exist in tests/docker/targets.tsv.
PACKAGE_DIR must contain externally built Krema packages for that target.

Environment:
  DOCKER_HOST                 Docker daemon endpoint (default: tcp://localhost:2375)
  KREMA_DOCKER_IMAGE_PREFIX   Image prefix (default: krema/gui-runtime)
  KREMA_SMOKE_WIDTH           Host KWin virtual width (default: 800)
  KREMA_SMOKE_HEIGHT          Host KWin virtual height (default: 600)
  KREMA_SMOKE_TIMEOUT         Seconds Krema must stay alive (default: 10)
  KREMA_SMOKE_SETTLE_SECONDS  Seconds to wait before process/screenshot checks (default: 4)
  KREMA_SMOKE_READY_TIMEOUT   Seconds to wait for install to finish and krema to start (default: 180)
  KREMA_SMOKE_SCREENSHOT_DIR  Optional directory for KWin workspace BMP screenshots
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    usage
    exit 0
fi

target_id="${1:?missing TARGET_ID}"
package_dir="${2:?missing PACKAGE_DIR}"

if [[ ! -d "$package_dir" ]]; then
    echo "Package directory does not exist: $package_dir" >&2
    exit 66
fi

target_line=""
while IFS=$'\t' read -r row_target family base_image obs_repository package_glob; do
    [[ -z "${row_target:-}" || "${row_target:0:1}" == "#" ]] && continue
    if [[ "$row_target" == "$target_id" ]]; then
        target_line="$row_target"$'\t'"$family"$'\t'"$base_image"$'\t'"$obs_repository"$'\t'"$package_glob"
        break
    fi
done <"$targets_file"

if [[ -z "$target_line" ]]; then
    echo "Unknown target: $target_id" >&2
    exit 64
fi

IFS=$'\t' read -r _target_id _family _base_image _obs_repository package_glob <<<"$target_line"

host_runtime="$(mktemp -d /tmp/krema-wayland.XXXXXX)"
socket_name="krema-${target_id}-$$"
bus_file="$host_runtime/dbus-address"
kwin_pid=""
docker_pid=""
container_name="krema-smoke-$target_id-$$"
smoke_log="/tmp/krema-smoke-${target_id}-$$.log"

cleanup() {
    if [[ -n "$docker_pid" ]] && kill -0 "$docker_pid" >/dev/null 2>&1; then
        DOCKER_HOST="$docker_host" docker kill "$container_name" >/dev/null 2>&1 || true
        wait "$docker_pid" >/dev/null 2>&1 || true
    fi
    if [[ -n "$kwin_pid" ]] && kill -0 "$kwin_pid" >/dev/null 2>&1; then
        kill "$kwin_pid" >/dev/null 2>&1 || true
        wait "$kwin_pid" >/dev/null 2>&1 || true
    fi
    rm -rf "$host_runtime"
}
trap cleanup EXIT INT TERM

capture_workspace_screenshot() {
    local output_file raw_file metadata width height stride bus_address
    output_file="$1"
    raw_file="${output_file}.raw"
    bus_address="$(<"$bus_file")"

    metadata="$(DBUS_SESSION_BUS_ADDRESS="$bus_address" busctl --user call \
        org.kde.KWin.ScreenShot2 \
        /org/kde/KWin/ScreenShot2 \
        org.kde.KWin.ScreenShot2 \
        CaptureWorkspace a{sv}h 0 3 3>"$raw_file")"

    [[ "$metadata" =~ \"width\"[[:space:]]u[[:space:]]([0-9]+) ]] && width="${BASH_REMATCH[1]}"
    [[ "$metadata" =~ \"height\"[[:space:]]u[[:space:]]([0-9]+) ]] && height="${BASH_REMATCH[1]}"
    [[ "$metadata" =~ \"stride\"[[:space:]]u[[:space:]]([0-9]+) ]] && stride="${BASH_REMATCH[1]}"

    if [[ -z "${width:-}" || -z "${height:-}" || -z "${stride:-}" ]]; then
        echo "Could not parse KWin screenshot metadata: $metadata" >&2
        return 69
    fi
    if ! command -v python3 >/dev/null 2>&1; then
        echo 'python3 is required to convert KWin raw screenshot data to BMP' >&2
        return 69
    fi

    python3 - "$raw_file" "$output_file" "$width" "$height" "$stride" <<'PY'
import struct
import sys

raw_path, bmp_path, width_text, height_text, stride_text = sys.argv[1:]
width = int(width_text)
height = int(height_text)
stride = int(stride_text)
row_size = ((width * 3 + 3) // 4) * 4
pixel_size = row_size * height
file_size = 54 + pixel_size

with open(raw_path, 'rb') as raw_file:
    raw = raw_file.read()

if len(raw) < stride * height:
    raise SystemExit(f'raw screenshot too short: {len(raw)} < {stride * height}')

with open(bmp_path, 'wb') as bmp:
    bmp.write(struct.pack('<2sIHHI', b'BM', file_size, 0, 0, 54))
    bmp.write(struct.pack('<IiiHHIIiiII', 40, width, -height, 1, 24, 0, pixel_size, 2835, 2835, 0, 0))
    padding = b'\0' * (row_size - width * 3)
    for y in range(height):
        row = raw[y * stride:y * stride + width * 4]
        out = bytearray()
        for x in range(width):
            blue, green, red, _alpha = row[x * 4:x * 4 + 4]
            out.extend((blue, green, red))
        bmp.write(out)
        bmp.write(padding)
PY
    rm -f "$raw_file"
}

chmod 700 "$host_runtime"

env \
    XDG_RUNTIME_DIR="$host_runtime" \
    KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 \
    KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1 \
    LIBGL_ALWAYS_SOFTWARE=1 \
    GALLIUM_DRIVER=llvmpipe \
    QT_QUICK_BACKEND=software \
    dbus-run-session -- \
    sh -c 'printf "%s" "$DBUS_SESSION_BUS_ADDRESS" >"$1"; exec kwin_wayland --virtual --no-lockscreen --socket "$2" --width "$3" --height "$4"' \
    sh "$bus_file" "$socket_name" "$screen_width" "$screen_height" \
    >/tmp/krema-kwin-${target_id}.log 2>&1 &
kwin_pid="$!"

for _ in {1..80}; do
    if [[ -S "$host_runtime/$socket_name" && -s "$bus_file" ]]; then
        break
    fi
    sleep 0.25
done

if [[ ! -S "$host_runtime/$socket_name" || ! -s "$bus_file" ]]; then
    echo "KWin virtual Wayland socket did not appear: $host_runtime/$socket_name" >&2
    tail -n 120 "/tmp/krema-kwin-${target_id}.log" >&2 || true
    exit 67
fi

DOCKER_HOST="$docker_host" docker run --rm \
    --name "$container_name" \
    -e XDG_RUNTIME_DIR=/tmp/krema-runtime \
    -e WAYLAND_DISPLAY="$socket_name" \
    -e KREMA_PACKAGE_DIR=/packages \
    -e KREMA_PACKAGE_GLOB="$package_glob" \
    -e QT_QPA_PLATFORM=wayland \
    -e QT_QUICK_BACKEND=software \
    -e LIBGL_ALWAYS_SOFTWARE=1 \
    -e GALLIUM_DRIVER=llvmpipe \
    -v "$host_runtime/$socket_name:/tmp/krema-runtime/$socket_name" \
    -v "$(cd -- "$package_dir" && pwd):/packages:ro" \
    "$image_prefix:$target_id" \
    --timeout "$smoke_timeout" \
    >"$smoke_log" 2>&1 &
docker_pid="$!"

ready_deadline=$((SECONDS + smoke_ready_timeout))
top_output=""
while ((SECONDS < ready_deadline)); do
    if top_output="$(DOCKER_HOST="$docker_host" docker top "$container_name" 2>&1)"; then
        if printf '%s\n' "$top_output" | rg -q '(^|[[:space:]/])krema([[:space:]]|$)'; then
            break
        fi
    elif ! kill -0 "$docker_pid" >/dev/null 2>&1; then
        set +e
        wait "$docker_pid"
        status="$?"
        set -e
        docker_pid=""
        cat "$smoke_log" >&2 || true
        exit "$status"
    fi
    sleep 1
done

if ! printf '%s\n' "$top_output" | rg -q '(^|[[:space:]/])krema([[:space:]]|$)'; then
    echo "Krema process did not start in container $container_name before ${smoke_ready_timeout}s" >&2
    printf '%s\n' "$top_output" >&2
    cat "$smoke_log" >&2 || true
    exit 68
fi

sleep "$smoke_settle_seconds"

if [[ -n "$screenshot_dir" ]]; then
    mkdir -p "$screenshot_dir"
    capture_workspace_screenshot "$screenshot_dir/${target_id}.bmp"
    echo "Saved screenshot: $screenshot_dir/${target_id}.bmp"
fi

set +e
wait "$docker_pid"
status="$?"
set -e
docker_pid=""

if ((status != 0)); then
    cat "$smoke_log" >&2 || true
    exit "$status"
fi

cat "$smoke_log"
