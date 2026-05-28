#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: krema-container-smoke [--install-only] [--no-install] [--timeout SECONDS]

Environment:
  KREMA_PACKAGE_DIR   Directory containing externally built Krema packages (default: /packages)
  KREMA_PACKAGE_GLOB  Package glob from targets.tsv (default: auto by package manager)
  WAYLAND_DISPLAY     Mounted host KWin virtual Wayland socket name (required for run smoke)
  XDG_RUNTIME_DIR     Container runtime dir containing WAYLAND_DISPLAY (default: /tmp/krema-runtime)

The image intentionally does not contain Krema. Mount osc/build artifacts at
/packages and this script installs them inside the container before launching
krema against the mounted virtual Wayland display.
EOF
}

install_only=0
skip_install=0
smoke_timeout=10

while (($#)); do
    case "$1" in
        --help|-h)
            usage
            exit 0
            ;;
        --install-only)
            install_only=1
            shift
            ;;
        --no-install)
            skip_install=1
            shift
            ;;
        --timeout)
            smoke_timeout="${2:?missing timeout value}"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 64
            ;;
    esac
done

package_dir="${KREMA_PACKAGE_DIR:-/packages}"
package_glob="${KREMA_PACKAGE_GLOB:-}"

detect_glob() {
    if command -v zypper >/dev/null 2>&1 || command -v dnf >/dev/null 2>&1; then
        printf '%s\n' '*.rpm'
    elif command -v apt-get >/dev/null 2>&1; then
        printf '%s\n' '*.deb'
    elif command -v pacman >/dev/null 2>&1; then
        printf '%s\n' '*.pkg.tar.zst'
    else
        echo 'No supported package manager found' >&2
        exit 65
    fi
}

install_packages() {
    local glob
    local -a packages=()
    glob="${package_glob:-$(detect_glob)}"

    if [[ ! -d "$package_dir" ]]; then
        echo "Package directory does not exist: $package_dir" >&2
        exit 66
    fi

    shopt -s nullglob
    for package in "$package_dir"/$glob; do
        packages+=("$package")
    done
    shopt -u nullglob

    if ((${#packages[@]} == 0)); then
        echo "No Krema package matched $package_dir/$glob" >&2
        exit 66
    fi

    if command -v zypper >/dev/null 2>&1; then
        zypper --non-interactive --no-gpg-checks install "${packages[@]}"
    elif command -v dnf >/dev/null 2>&1; then
        dnf -y install "${packages[@]}"
    elif command -v apt-get >/dev/null 2>&1; then
        apt-get update
        apt-get install -y "${packages[@]}"
    elif command -v pacman >/dev/null 2>&1; then
        pacman -U --noconfirm "${packages[@]}"
    fi
}

prepare_runtime_dir() {
    export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/krema-runtime}"
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 700 "$XDG_RUNTIME_DIR"
}

run_smoke() {
    if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
        echo 'WAYLAND_DISPLAY is required for GUI smoke test' >&2
        exit 67
    fi
    if [[ ! -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]]; then
        echo "Wayland socket is not available: $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" >&2
        exit 67
    fi
    if ! command -v krema >/dev/null 2>&1; then
        echo 'krema command is not installed' >&2
        exit 68
    fi

    export QT_QPA_PLATFORM=wayland
    export QT_QUICK_BACKEND="${QT_QUICK_BACKEND:-software}"
    export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
    export GALLIUM_DRIVER="${GALLIUM_DRIVER:-llvmpipe}"

    if command -v wayland-info >/dev/null 2>&1; then
        timeout 5s wayland-info >/tmp/krema-wayland-info.txt
    fi

    set +e
    timeout --preserve-status "${smoke_timeout}s" dbus-run-session -- krema >/tmp/krema-smoke.log 2>&1
    local status=$?
    set -e

    local stayed_alive_statuses=(124 143 0)
    if [[ " ${stayed_alive_statuses[*]} " == *" $status "* ]]; then
        echo "Krema GUI smoke passed with status $status"
        exit 0
    fi

    echo "Krema GUI smoke failed with status $status" >&2
    tail -n 80 /tmp/krema-smoke.log >&2 || true
    exit "$status"
}

prepare_runtime_dir
if ((skip_install == 0)); then
    install_packages
fi
if ((install_only == 1)); then
    exit 0
fi
run_smoke
