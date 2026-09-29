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
# 1. In the background: gets the target's runtime image (image-ref.sh
#    runtime <target>: the local tag, else ghcr.io, else a local build with
#    build-ci-image.sh), starts a container of it and, on rolling targets,
#    fully upgrades it (packages.sh upgrade).
# 2. Meanwhile builds the package with tests/distro/build-package.sh into
#    tests/distro/.cache/packages/<target>/ unless one is already there.
# 3. Installs the package into that container through the distro package
#    manager (packages.sh krema: its Requires/Depends must pull in krema's
#    runtime) and commits it as krema-e2e-distro:<target>.
# 4. Runs tests/appium/run-e2e.sh with that image and KREMA_E2E_BINARY, so
#    the session setup is exactly Tier 2's.
#
# Every phase prints `[distro] <phase>: <seconds>s`.
#
# Environment:
#   KREMA_DISTRO_REBUILD_PACKAGE=1   rebuild the package even if cached
#   KREMA_DISTRO_SKIP_IMAGE_BUILD=1  reuse an existing krema-e2e-distro:<target>
#                                    (steps 1 and 3 are skipped)
#   KREMA_CCACHE_DIR, CCACHE_MAXSIZE, KREMA_CI_REGISTRY, KREMA_ARCH_IMAGE
#                         see build-package.sh and image-ref.sh
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
# shellcheck source=tests/distro/image-ref.sh
. "$here/image-ref.sh"

usage() {
    awk 'NR > 4 && /^#/ { sub(/^# ?/, ""); print; next } NR > 4 { exit }' "${BASH_SOURCE[0]}" >&2
    exit 64
}

[[ $# -ge 1 ]] || usage
target="$1"
shift

distro_target "$target" || {
    echo "error: unknown target '$target' (not in tests/distro/targets.tsv, and not 'arch')" >&2
    exit 64
}

if [[ -z "${KREMA_E2E_PLATFORM:-}" ]]; then
    platform="$(distro_platform "$target")"
    [[ -z "$platform" ]] || export KREMA_E2E_PLATFORM="$platform"
fi
platform_args=()
[[ -n "${KREMA_E2E_PLATFORM:-}" ]] && platform_args=(--platform "$KREMA_E2E_PLATFORM")

now() { date +%s; }
run_start=$(now)

packages=/usr/local/lib/krema-distro/packages.sh
image="krema-e2e-distro:$target"
skip_image="${KREMA_DISTRO_SKIP_IMAGE_BUILD:-0}"

# ---------------------------------------------------------------------------
# Runtime container (background) overlapped with the package build
# ---------------------------------------------------------------------------

# The runtime image pull (or local build) and a rolling target's upgrade do
# not need the package: they run in the background while it builds, in the
# container the package is later installed into.
prep_ctr="krema-e2e-prep-$target-$$"
prep_pid=
prep_log=
shard_state=

start_prep() {
    local ref
    ref="$(ci_image_ref runtime "$target")"
    prep_log="$(mktemp -t krema-distro-prep.XXXXXX)"
    (
        t0=$(now)
        KREMA_DOCKER_PLATFORM="${KREMA_E2E_PLATFORM:-}" \
            "$here/build-ci-image.sh" --pull runtime "$target" >/dev/null
        echo "[distro] runtime-image: $(($(now) - t0))s"
        t0=$(now)
        docker run -d --init --name "$prep_ctr" "${platform_args[@]}" "$ref" sleep infinity >/dev/null
        docker exec "$prep_ctr" sh "$packages" upgrade
        echo "[distro] runtime-upgrade: $(($(now) - t0))s"
        # While the package builds, prefetch the deps the packaging declares
        # (metadata refresh + downloads, no install) so `packages.sh krema`
        # finds them in the package-manager cache.
        t0=$(now)
        docker exec "$prep_ctr" mkdir -p /tmp/krema-pkgdefs
        docker cp "$repo/packaging/." "$prep_ctr:/tmp/krema-pkgdefs"
        docker exec -e PKGDEFS=/tmp/krema-pkgdefs "$prep_ctr" sh "$packages" depfetch
        echo "[distro] dep-fetch: $(($(now) - t0))s"
    ) >"$prep_log" 2>&1 &
    prep_pid=$!
    runtime_ref="$ref"
}

cleanup() {
    if [[ -n "$prep_pid" ]]; then
        kill "$prep_pid" 2>/dev/null || true
        wait "$prep_pid" 2>/dev/null || true
        prep_pid=
    fi
    docker rm -f "$prep_ctr" >/dev/null 2>&1 || true
    rm -f "${prep_log:-}"
    rm -rf "${shard_state:-}"
}
trap cleanup EXIT

# Blocks until the background preparation finishes; on failure shows its
# whole output and exits with its status.
await_prep() {
    local rc=0
    wait "$prep_pid" || rc=$?
    prep_pid=
    if (( rc != 0 )); then
        cat "$prep_log" >&2 || true
        exit "$rc"
    fi
    grep -E '^\[(distro|ci-image)\]' "$prep_log" || true
}

[[ "$skip_image" == 1 ]] || start_prep

pkg_dir="$here/.cache/packages/$target"
shopt -s nullglob
pkgs=("$pkg_dir"/*.rpm "$pkg_dir"/*.deb "$pkg_dir"/*.pkg.tar.zst)
shopt -u nullglob
if [[ "${KREMA_DISTRO_REBUILD_PACKAGE:-0}" == 1 || ${#pkgs[@]} -eq 0 ]]; then
    t0=$(now)
    rm -rf "$pkg_dir"
    KREMA_DOCKER_PLATFORM="${KREMA_E2E_PLATFORM:-}" \
        "$here/build-package.sh" "$target" "$here/.cache/packages"
    echo "[distro] package: $(($(now) - t0))s"
fi

# ---------------------------------------------------------------------------
# Package install into the runtime container -> krema-e2e-distro:<target>
# ---------------------------------------------------------------------------

if [[ "$skip_image" != 1 ]]; then
    t0=$(now)
    await_prep
    echo "[distro] runtime-wait: $(($(now) - t0))s"

    t0=$(now)
    install_log="$(mktemp -t krema-distro-install.XXXXXX)"
    if ! {
        docker cp "$pkg_dir/." "$prep_ctr:/pkgs" &&
            docker exec "$prep_ctr" sh -c "sh $packages krema && rm -rf /pkgs"
    } >"$install_log" 2>&1; then
        cat "$install_log" >&2
        rm -f "$install_log"
        exit 1
    fi
    rm -f "$install_log"
    echo "[distro] package-install: $(($(now) - t0))s"

    # The container runs `sleep infinity`; give the image back the runtime
    # image's CMD (run-e2e.sh passes its own command anyway).
    t0=$(now)
    changes=(--change 'WORKDIR /work')
    cmd="$(docker image inspect --format '{{json .Config.Cmd}}' "$runtime_ref")"
    [[ "$cmd" == null ]] || changes+=(--change "CMD $cmd")
    docker commit "${changes[@]}" "$prep_ctr" "$image" >/dev/null
    docker rm -f "$prep_ctr" >/dev/null
    echo "[distro] image-commit: $(($(now) - t0))s"
elif ! docker image inspect "$image" >/dev/null 2>&1; then
    echo "error: KREMA_DISTRO_SKIP_IMAGE_BUILD=1 but there is no image $image" >&2
    exit 1
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

suite_start=$(now)
finish() {
    echo "[distro] suite: $(($(now) - suite_start))s"
    echo "[distro] total: $(($(now) - run_start))s"
    exit "$1"
}

if (( shards == 1 )); then
    rc=0
    "$repo/tests/appium/run-e2e.sh" "$@" || rc=$?
    finish "$rc"
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
finish "$rc"
