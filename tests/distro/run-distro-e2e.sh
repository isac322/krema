#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Tier 3: run the Tier 2 AT-SPI E2E suite (tests/appium) against Krema
# installed from its distro package, on that distro's own KWin/Qt/KF.
#
#   tests/distro/run-distro-e2e.sh <target>                    # full suite
#   tests/distro/run-distro-e2e.sh <target> test_smoke.py -x   # pytest args
#   tests/distro/run-distro-e2e.sh <target> --shell            # debug shell
#
# <target> is a row of tests/docker/targets.tsv or `arch`.
#
# 1. Builds the package with tests/distro/build-package.sh into
#    tests/distro/.cache/packages/<target>/ unless one is already there.
# 2. Builds image krema-e2e-distro:<target> (tests/distro/image/Dockerfile):
#    the target's kwin_wayland/PipeWire/AT-SPI + selenium-webdriver-at-spi
#    built on that distro + the package installed by the package manager.
# 3. Runs tests/appium/run-e2e.sh with that image and KREMA_E2E_BINARY, so
#    the session setup is exactly Tier 2's.
#
# Environment:
#   KREMA_DISTRO_REBUILD_PACKAGE=1   rebuild the package even if cached
#   KREMA_DISTRO_SKIP_IMAGE_BUILD=1  reuse an existing krema-e2e-distro:<target>
#   KREMA_E2E_ARTIFACTS   default tests/appium/artifacts-distro-<target>
#   KREMA_E2E_PLATFORM    default linux/amd64 for arch and opensuse-slowroll
#                         (their repositories are amd64-only), else native
#   Any other KREMA_E2E_* variable of tests/appium/run-e2e.sh.

set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd -- "$here/../.." && pwd)"

usage() {
    sed -n '2,28p' "${BASH_SOURCE[0]}" | sed 's/^# \?//' >&2
    exit 64
}

[[ $# -ge 1 ]] || usage
target="$1"
shift

# target -> family and base image; must match build-package.sh so the package
# is built and tested on the same base.
if [[ "$target" == arch ]]; then
    family=arch
    base_image="${KREMA_ARCH_IMAGE:-docker.io/archlinux:latest}"
else
    row="$(awk -F'\t' -v id="$target" '
        /^[[:space:]]*#/ || NF < 4 { next }
        $1 == id {
            image = "docker.io/" $3
            if ($4 != "" && $4 != "pending") image = image "@" $4
            print $2 "\t" image
            found = 1
        }
        END { exit(found ? 0 : 1) }
    ' "$repo/tests/docker/targets.tsv")" || {
        echo "error: unknown target '$target' (not in tests/docker/targets.tsv, and not 'arch')" >&2
        exit 64
    }
    family="${row%%$'\t'*}"
    base_image="${row#*$'\t'}"
fi

if [[ -z "${KREMA_E2E_PLATFORM:-}" ]] && [[ "$target" == arch || "$target" == opensuse-slowroll ]]; then
    export KREMA_E2E_PLATFORM=linux/amd64
fi
platform_args=()
[[ -n "${KREMA_E2E_PLATFORM:-}" ]] && platform_args=(--platform "$KREMA_E2E_PLATFORM")

now() { date +%s; }

pkg_dir="$here/.cache/packages/$target"
shopt -s nullglob
pkgs=("$pkg_dir"/*.rpm "$pkg_dir"/*.deb "$pkg_dir"/*.pkg.tar.zst)
shopt -u nullglob
if [[ "${KREMA_DISTRO_REBUILD_PACKAGE:-0}" == 1 || ${#pkgs[@]} -eq 0 ]]; then
    t0=$(now)
    rm -rf "$pkg_dir"
    KREMA_DOCKER_PLATFORM="${KREMA_E2E_PLATFORM:-}" \
        "$here/build-package.sh" "$target" "$here/.cache/packages"
    echo "[distro] package $target: $(($(now) - t0))s"
fi

image="krema-e2e-distro:$target"
if [[ "${KREMA_DISTRO_SKIP_IMAGE_BUILD:-0}" != 1 ]]; then
    t0=$(now)
    build=(docker buildx build --load "${platform_args[@]}"
        --build-arg "BASE_IMAGE=$base_image"
        --build-arg "TARGET_ID=$target"
        --build-arg "FAMILY=$family"
        --build-context "appium-tools=$repo/tests/appium/tools"
        --build-context "packages=$pkg_dir"
        -t "$image" "$here/image")
    if ! "${build[@]}" -q >/dev/null; then
        # Re-run with output so the failure is visible.
        "${build[@]}"
        exit 1
    fi
    echo "[distro] image $image ready: $(($(now) - t0))s"
fi

export KREMA_E2E_IMAGE="$image"
export KREMA_E2E_SKIP_BUILD=1
export KREMA_E2E_BINARY=/usr/bin/krema
export KREMA_E2E_DISTRO="$target"
export KREMA_E2E_ARTIFACTS="${KREMA_E2E_ARTIFACTS:-$repo/tests/appium/artifacts-distro-$target}"
exec "$repo/tests/appium/run-e2e.sh" "$@"
