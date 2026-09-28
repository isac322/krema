#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Host entrypoint for the Krema AT-SPI E2E suite.
#
#   tests/appium/run-e2e.sh                         # every tests/appium/test_*.py
#   tests/appium/run-e2e.sh test_smoke.py           # one file
#   tests/appium/run-e2e.sh 'test_smoke.py::test_zoom' -x   # any pytest args
#   tests/appium/run-e2e.sh --shell                 # debug shell in the container
#
# Builds (or reuses) the image, syncs the checkout into the container, builds
# krema incrementally in a named volume, runs pytest under a private
# kwin_wayland --virtual session and writes JUnit XML, screenshots and logs to
# tests/appium/artifacts/. Needs only an unprivileged `docker run`.
#
# Environment:
#   KREMA_E2E_IMAGE          image tag (default krema-e2e:local)
#   KREMA_E2E_SKIP_BUILD=1   do not (re)build the image
#   KREMA_E2E_VOLUME         build volume (default krema-e2e-build-<arch>)
#   KREMA_E2E_ARTIFACTS      artifacts directory (default tests/appium/artifacts)
#   KREMA_E2E_PLATFORM       e.g. linux/amd64 (default: native)
#   KREMA_E2E_SCREEN_WIDTH, KREMA_E2E_SCREEN_HEIGHT   virtual output size
#   KREMA_E2E_OUTPUT_COUNT   number of outputs (default 1); tests marked
#                            @pytest.mark.outputs(n) need n
#   KREMA_E2E_KWIN_BACKEND   auto (default), virtual or drm; see below
#   KREMA_E2E_DOCKER_ARGS    extra `docker run` arguments, e.g. "--cpus=2" to
#                            approximate a slow CI runner (default: none)
#   KREMA_E2E_BINARY         absolute path of an installed krema inside the
#                            image (e.g. /usr/bin/krema): skips the source
#                            sync and krema build, runs the suite from the
#                            read-only checkout (tests/distro/run-distro-e2e.sh)
#   KREMA_E2E_DISTRO         target id exported to the tests (tests/distro)
#
# If the host has /dev/dri (e.g. after `modprobe vgem`), it is passed through
# so KWin composites with OpenGL: needed for screenshots and PipeWire
# window thumbnails. Without it KWin falls back to QPainter. With a render
# node (vgem) kwin runs `--virtual` and KREMA_E2E_OUTPUT_COUNT maps to
# --output-count; with only KMS cards (`modprobe vkms`, e.g. GitHub-hosted
# runners) it drives one card with its DRM backend and the output count is
# that card's number of connected connectors (a multi-output vkms device
# comes from tests/appium/setup-vkms.sh). KWIN_DRM_DEVICES pins the card.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
image=${KREMA_E2E_IMAGE:-krema-e2e:local}
arch=$(uname -m)
volume=${KREMA_E2E_VOLUME:-krema-e2e-build-$arch}
artifacts=${KREMA_E2E_ARTIFACTS:-$here/artifacts}

platform_args=
if [ -n "${KREMA_E2E_PLATFORM:-}" ]; then
    platform_args="--platform $KREMA_E2E_PLATFORM"
    volume=${KREMA_E2E_VOLUME:-krema-e2e-build-$(echo "$KREMA_E2E_PLATFORM" | tr '/' '-')}
fi

now() { date +%s; }

if [ "${KREMA_E2E_SKIP_BUILD:-0}" != "1" ]; then
    t0=$(now)
    # shellcheck disable=SC2086
    if ! docker build $platform_args -q -t "$image" "$here" >/dev/null; then
        # Re-run with output so the failure is visible.
        # shellcheck disable=SC2086
        docker build $platform_args -t "$image" "$here"
        exit 1
    fi
    echo "[e2e] image $image ready: $(($(now) - t0))s"
fi

# Test files may be given relative to the repo root or to tests/appium.
for a do
    shift
    set -- "$@" "${a#tests/appium/}"
done

rm -rf "$artifacts"
mkdir -p "$artifacts"
# docker -v needs an absolute path (relative KREMA_E2E_ARTIFACTS would be a
# named volume).
artifacts=$(cd "$artifacts" && pwd -P)

dri_args=
if [ -d /dev/dri ]; then
    dri_args="--device /dev/dri"
fi

tty_args=
if [ -t 0 ] && [ -t 1 ]; then
    tty_args=-it
fi

build_args="-v $volume:/build"
if [ -n "${KREMA_E2E_BINARY:-}" ]; then
    build_args=
fi

# shellcheck disable=SC2086
docker run --rm --init $tty_args $platform_args \
    --shm-size=512m \
    -v "$repo:/src:ro" \
    $build_args \
    -v "$artifacts:/artifacts" \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
    -e KREMA_E2E_SCREEN_WIDTH -e KREMA_E2E_SCREEN_HEIGHT -e KREMA_E2E_OUTPUT_COUNT \
    -e KREMA_E2E_KWIN_BACKEND -e KWIN_DRM_DEVICES \
    -e KREMA_E2E_BINARY -e KREMA_E2E_DISTRO \
    $dri_args ${KREMA_E2E_DOCKER_ARGS:-} \
    "$image" sh /src/tests/appium/entrypoint.sh "$@"
