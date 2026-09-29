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
# <target> is a row of tests/distro/targets.tsv or `arch`.
#
# 1. Builds the package with tests/distro/build-package.sh into
#    tests/distro/.cache/packages/<target>/ unless one is already there.
#    While the package builds, the image stages that do not depend on it
#    (base + swas-build) build in the background into BuildKit's cache, so
#    step 2 only runs the package-install layer.
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
#   KREMA_E2E_SHARDS=N    run the suite as N shards concurrently (default 1,
#                         one run-e2e.sh). Shard i (0-based) gets
#                         KREMA_E2E_SHARD=i/N, selects every N-th collected
#                         test, and writes to <artifacts>/shard-<i>/ with a
#                         [shard i] log prefix; any shard failing fails the
#                         run. --shell cannot be sharded.
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
    ' "$here/targets.tsv")" || {
        echo "error: unknown target '$target' (not in tests/distro/targets.tsv, and not 'arch')" >&2
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

# ---------------------------------------------------------------------------
# Package build overlapped with the package-independent image stages
# ---------------------------------------------------------------------------

# The package build takes ~2 min (clean container, distro package manager,
# compile). The image's base and swas-build stages only need BASE_IMAGE,
# TARGET_ID, FAMILY and the appium-tools build context — not the package —
# and take a similar time. Build them in the background while the package
# builds; with the default docker driver every buildx build shares the
# daemon's BuildKit cache, so the --load build below then reuses those
# stages and only runs the last (package-install) layer.

prebuild_pid=
prebuild_log=
shard_state=

start_prebuild() {
    prebuild_log="$(mktemp -t krema-distro-prebuild.XXXXXX)"
    # The packages build context is deliberately absent: it is only consumed
    # by the last stage, and pkg_dir may not exist yet.
    docker buildx build --target swas-build "${platform_args[@]}" \
        --build-arg "BASE_IMAGE=$base_image" \
        --build-arg "TARGET_ID=$target" \
        --build-arg "FAMILY=$family" \
        --build-context "appium-tools=$repo/tests/appium/tools" \
        "$here/image" >"$prebuild_log" 2>&1 &
    prebuild_pid=$!
}

prebuild_cleanup() {
    if [[ -n "$prebuild_pid" ]]; then
        kill "$prebuild_pid" 2>/dev/null || true
        wait "$prebuild_pid" 2>/dev/null || true
        prebuild_pid=
    fi
    rm -f "${prebuild_log:-}"
    rm -rf "${shard_state:-}"
}
trap prebuild_cleanup EXIT

# Blocks until the background stage build finishes; propagates its failure
# (with the captured build output, like the re-run below does for the full
# build). Already reaped/exited processes are ignored.
await_prebuild() {
    [[ -z "$prebuild_pid" ]] && return 0
    local rc=0
    wait "$prebuild_pid" || rc=$?
    prebuild_pid=
    if (( rc != 0 )); then
        cat "$prebuild_log" >&2 || true
        rm -f "${prebuild_log:-}"
        exit "$rc"
    fi
    rm -f "${prebuild_log:-}"
}

pkg_dir="$here/.cache/packages/$target"
shopt -s nullglob
pkgs=("$pkg_dir"/*.rpm "$pkg_dir"/*.deb "$pkg_dir"/*.pkg.tar.zst)
shopt -u nullglob
if [[ "${KREMA_DISTRO_REBUILD_PACKAGE:-0}" == 1 || ${#pkgs[@]} -eq 0 ]]; then
    # While the package builds, prebuild every image stage that does not
    # need it (see start_prebuild). Skipped when the image build is skipped:
    # nothing would consume the warmed cache.
    [[ "${KREMA_DISTRO_SKIP_IMAGE_BUILD:-0}" == 1 ]] || start_prebuild
    t0=$(now)
    rm -rf "$pkg_dir"
    KREMA_DOCKER_PLATFORM="${KREMA_E2E_PLATFORM:-}" \
        "$here/build-package.sh" "$target" "$here/.cache/packages"
    echo "[distro] package $target: $(($(now) - t0))s"
fi

image="krema-e2e-distro:$target"
if [[ "${KREMA_DISTRO_SKIP_IMAGE_BUILD:-0}" != 1 ]]; then
    # Overlapped stage build must be done (and known-good) before the full
    # build starts consuming its cache.
    await_prebuild
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

# ---------------------------------------------------------------------------
# Suite run: single container, or KREMA_E2E_SHARDS shards concurrently
# ---------------------------------------------------------------------------

shards="${KREMA_E2E_SHARDS:-1}"
[[ "$shards" =~ ^[0-9]+$ ]] && (( shards >= 1 )) || {
    echo "error: KREMA_E2E_SHARDS must be a positive integer (got '$shards')" >&2
    exit 64
}

if (( shards == 1 )); then
    exec "$repo/tests/appium/run-e2e.sh" "$@"
fi

# An interactive debug shell cannot be sharded.
for arg in "$@"; do
    [[ "$arg" != --shell ]] || {
        echo "error: --shell cannot be sharded (KREMA_E2E_SHARDS=$shards)" >&2
        exit 64
    }
done

# Each shard: one run-e2e.sh (one container, own kwin session) selecting
# KREMA_E2E_SHARD=i/N, artifacts in <artifacts>/shard-<i>, its own build
# volume when KREMA_E2E_VOLUME is set (the default is suffixed inside
# run-e2e.sh). Status goes to a file (the output pipeline would mask it);
# stdout/stderr is prefixed so the shards' lines stay distinguishable.
rm -rf "$KREMA_E2E_ARTIFACTS"
mkdir -p "$KREMA_E2E_ARTIFACTS"
shard_dir="$(mktemp -d -t krema-e2e-shards.XXXXXX)"
shard_state="$shard_dir"
# Split the CPUs between the shards' llvmpipe renderers (kwin and krema);
# by default each would start one thread per CPU and oversubscribe them.
threads=$(($(getconf _NPROCESSORS_ONLN) / shards))
((threads >= 1)) || threads=1
export LP_NUM_THREADS="${LP_NUM_THREADS:-$threads}"

for (( i = 0; i < shards; i++ )); do
    (
        export KREMA_E2E_SHARD="$i/$shards"
        export KREMA_E2E_ARTIFACTS="$KREMA_E2E_ARTIFACTS/shard-$i"
        [[ -z "${KREMA_E2E_VOLUME:-}" ]] ||
            export KREMA_E2E_VOLUME="${KREMA_E2E_VOLUME}-shard-$i"
        "$repo/tests/appium/run-e2e.sh" "$@" 2>&1
        echo "$?" >"$shard_dir/rc.$i"
    ) | awk -v s="$i" '{ print "[shard " s "] " $0; fflush() }' &
done

wait

rc=0
for (( i = 0; i < shards; i++ )); do
    shard_rc="missing"
    [[ -f "$shard_dir/rc.$i" ]] && shard_rc="$(<"$shard_dir/rc.$i")"
    [[ "$shard_rc" == 0 ]] || rc=1
done
rm -rf "$shard_dir"
exit "$rc"
