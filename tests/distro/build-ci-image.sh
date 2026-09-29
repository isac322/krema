#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Build a Krema CI image locally and tag it with its tests/distro/image-ref.sh
# reference.
#
#   tests/distro/build-ci-image.sh [--pull] runtime <target>
#   tests/distro/build-ci-image.sh [--pull] builder <target>
#   tests/distro/build-ci-image.sh [--pull] ctest
#
# Without --pull it always builds (`docker buildx build --load`): the
# publisher (.github/workflows/ci-images.yml) builds and then pushes the ref.
# With --pull it reuses the image when the ref already exists locally for
# the right platform, else pulls it, and builds only when neither works (no
# such tag, no registry access, offline, other architecture). Correctness
# never depends on the registry.
#
# Prints the reference on stdout; build and pull output goes to stderr.
#
#   runtime   tests/distro/image/Dockerfile --target runtime: the target's
#             Tier 2 runtime (kwin_wayland, PipeWire, AT-SPI, fixtures,
#             selenium-webdriver-at-spi built on that distro), without krema
#   builder   tests/distro/image/Dockerfile --target builder: packaging
#             toolchain, ccache and krema's build deps as of packaging/ when
#             the image was built
#   ctest     tests/appium/Dockerfile --target ctest-image
#
# The vgem image is built by the workflow itself; image-ref.sh names it.
#
# Environment:
#   KREMA_DOCKER_PLATFORM  container platform (default: linux/amd64 for arch
#                          and opensuse-slowroll, else native)
#   KREMA_CI_REGISTRY, KREMA_ARCH_IMAGE  see image-ref.sh

set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd -- "$here/../.." && pwd)"
# shellcheck source=tests/distro/image-ref.sh
. "$here/image-ref.sh"

usage() {
    awk 'NR > 4 && /^#/ { sub(/^# ?/, ""); print; next } NR > 4 { exit }' "${BASH_SOURCE[0]}" >&2
    exit 64
}

pull=0
if [[ "${1:-}" == --pull ]]; then
    pull=1
    shift
fi
kind="${1:-}"
target="${2:-}"
case "$kind" in
    runtime | builder) (($# == 2)) || usage ;;
    ctest) (($# == 1)) || usage ;;
    *) usage ;;
esac

ref="$(ci_image_ref "$kind" "$target")"

platform="${KREMA_DOCKER_PLATFORM:-}"
[[ -n "$platform" || "$kind" == ctest ]] || platform="$(distro_platform "$target")"
platform_args=()
[[ -z "$platform" ]] || platform_args=(--platform "$platform")
want_platform="$platform"
[[ -n "$want_platform" ]] || want_platform="$(docker version --format '{{.Server.Os}}/{{.Server.Arch}}')"

# local_ok: the ref exists locally and was built for want_platform.
local_ok() {
    local have
    have="$(docker image inspect --format '{{.Os}}/{{.Architecture}}' "$ref" 2>/dev/null)" || return 1
    [[ "$have" == "$want_platform" ]]
}

if ((pull)); then
    if local_ok; then
        echo "[ci-image] $ref: present locally" >&2
        echo "$ref"
        exit 0
    fi
    if docker pull --quiet --platform "$want_platform" "$ref" >&2 2>/dev/null; then
        if local_ok; then
            echo "[ci-image] $ref: pulled" >&2
            echo "$ref"
            exit 0
        fi
        # A single-platform image of another architecture: never run it.
        docker image rm "$ref" >/dev/null 2>&1 || true
    fi
    echo "[ci-image] $ref: not available for $want_platform, building locally" >&2
fi

label=(--label "org.opencontainers.image.source=https://github.com/isac322/krema")
case "$kind" in
    runtime | builder)
        distro_target "$target"
        docker buildx build --load "${platform_args[@]}" "${label[@]}" \
            --target "$kind" \
            --build-arg "BASE_IMAGE=$DISTRO_BASE_IMAGE" \
            --build-arg "TARGET_ID=$target" \
            --build-arg "FAMILY=$DISTRO_FAMILY" \
            --build-context "appium-tools=$repo/tests/appium/tools" \
            --build-context "packaging=$repo/packaging" \
            -t "$ref" "$here/image" >&2
        ;;
    ctest)
        docker buildx build --load "${platform_args[@]}" "${label[@]}" \
            --target ctest-image \
            -t "$ref" "$repo/tests/appium" >&2
        ;;
esac
echo "$ref"
