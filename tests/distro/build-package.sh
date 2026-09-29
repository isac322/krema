#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Build an installable Krema package for a Tier-3 distro E2E target inside a
# container of that target's builder image, using the repo's own packaging
# files. tests/distro/run-distro-e2e.sh calls it; it also works standalone.
#
#   tests/distro/build-package.sh <target-id> <outdir>
#   tests/distro/build-package.sh all <outdir>
#
# <target-id> is a row in tests/distro/targets.tsv, or `arch` which builds
# packaging/arch/PKGBUILD on archlinux:latest (amd64 image only).
#
# The package is built from the CURRENT source tree (the worktree minus VCS
# and build-output directories), packed as krema-<version>.tar.gz with the
# same root directory layout as the GitHub release tarball that OBS consumes
# via packaging/obs/_service (tar_scm). The build inside the container needs
# no network other than the distro package repositories.
#
# Builder image: `tests/distro/build-ci-image.sh --pull builder <target>`
# (the local tag, else ghcr.io, else a local build; see
# tests/distro/image-ref.sh). It already holds the packaging toolchain and
# the build deps; in the container, pkg/in-container.sh fully upgrades
# rolling targets, installs only the build deps a newer packaging/ adds
# (offline check first), and builds with the compilers behind ccache.
#
# Output: <outdir>/<target-id>/ contains the produced .rpm / .deb /
# .pkg.tar.zst binary package. Timing lines: `[distro] <phase>: <s>s`.
#
# Environment:
#   DOCKER_HOST            docker daemon to use (default: ambient socket)
#   KREMA_DOCKER_PLATFORM  force container platform, e.g. linux/amd64
#   KREMA_ARCH_IMAGE       arch base image (default: docker.io/archlinux:latest)
#   KREMA_CCACHE_DIR       host ccache directory, created if missing (default
#                          tests/distro/.cache/ccache/<target-id>); given back
#                          to the invoking user after the build
#   CCACHE_MAXSIZE         ccache size limit (default 500M)
#   KREMA_CI_REGISTRY      see tests/distro/image-ref.sh

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"
packaging_dir="$repo_root/packaging"
# shellcheck source=tests/distro/image-ref.sh
. "$script_dir/image-ref.sh"

usage() {
    awk 'NR > 4 && /^#/ { sub(/^# ?/, ""); print; next } NR > 4 { exit }' "${BASH_SOURCE[0]}" >&2
    exit 64
}

[[ $# -eq 2 ]] || usage
target_filter="$1"
outdir_arg="$2"

command -v docker >/dev/null || { echo "error: docker not found" >&2; exit 69; }
# GNU tar required (sort=/--transform/--exclude-vcs); macOS ships bsdtar.
tar_cmd=tar
if ! tar --version 2>/dev/null | grep -qi gnu; then
    if command -v gtar >/dev/null; then
        tar_cmd=gtar
    else
        echo "error: GNU tar required (install gtar on macOS)" >&2
        exit 69
    fi
fi

mkdir -p "$outdir_arg"
outdir="$(cd -- "$outdir_arg" && pwd)"

host_arch="$(uname -m)"

# ---------------------------------------------------------------------------
# Source tarball
# ---------------------------------------------------------------------------

# package_version <family> -> version string defined by the packaging files
package_version() {
    case "$1" in
        fedora|suse)
            awk '/^Version:[[:space:]]*/ { print $2; exit }' \
                "$packaging_dir/obs/krema.spec" ;;
        debian)
            sed -n '1s/^krema (\(.*\)) .*/\1/p' "$packaging_dir/obs/debian.changelog" \
                | cut -d- -f1 ;;
        arch)
            awk -F= '/^pkgver=/ { print $2; exit }' "$packaging_dir/arch/PKGBUILD" ;;
        *) return 1 ;;
    esac
}

# make_source_tarball <version> <dest-dir> -> writes krema-<version>.tar.gz
# The file set mirrors what OBS tar_scm produces (the git tree). git metadata
# is not required to be reachable here (worktree mounts may lack it), so the
# tree is packed directly with VCS and build-output directories excluded.
# Extra unignored files are harmless to a package build.
make_source_tarball() {
    local version="$1" dest="$2"
    local -a excludes=(
        --exclude-vcs
        --exclude='./build' --exclude='./build-*' --exclude='./cmake-build-*'
        --exclude='./out' --exclude='./node_modules' --exclude='./.idea'
        --exclude='./.vscode' --exclude='./.claude/worktrees' --exclude='./.omo'
        # Package cache and E2E run artifacts (screenshots, logs).
        --exclude='./tests/distro/.cache' --exclude='./tests/appium/artifacts*'
    )
    # Never package our own output directory if the caller put <outdir> inside
    # the repo.
    local rel
    if rel="$(realpath --relative-to="$repo_root" "$outdir" 2>/dev/null)" \
        && [[ "$rel" != ..* && "$rel" != "." ]]; then
        excludes+=(--exclude="./$rel")
    fi

    # Exit status 1 only means "file changed as we read it": shared-folder
    # mounts (virtiofs, e.g. Lima) report spurious directory ctime changes.
    # Every file is still archived; anything else is fatal.
    local rc=0
    "$tar_cmd" --create --gzip --file="$dest/krema-$version.tar.gz" \
        --sort=name --numeric-owner --owner=0 --group=0 \
        --mtime=@1700000000 \
        --warning=no-file-changed \
        "${excludes[@]}" \
        --transform="s,^\\./,krema-$version/," \
        -C "$repo_root" . || rc=$?
    (( rc <= 1 ))
}

# ---------------------------------------------------------------------------
# Container run
# ---------------------------------------------------------------------------

# run_build <target-id> <family> -> docker run of pkg/in-container.sh in the
# target's builder image. Runs under `if`, so errexit is off: every step
# checks its status.
run_build() {
    local id="$1" family="$2"
    local platform="${KREMA_DOCKER_PLATFORM:-}"

    if [[ -z "$platform" ]]; then
        platform="$(distro_platform "$id")"
        if [[ "$platform" == linux/amd64 && "$host_arch" != "x86_64" ]] \
            && [[ ! -e /proc/sys/fs/binfmt_misc/qemu-x86_64 ]] \
            && ! docker info --format '{{json .SecurityOptions}}' 2>/dev/null \
                | grep -q rosetta; then
            echo "error: '$id' needs a linux/amd64 container but this" \
                "host is $host_arch and no qemu-x86_64 binfmt handler is" \
                "registered (install qemu-user + binfmt support, or" \
                "run on an x86_64 host)" >&2
            return 65
        fi
    fi

    local image t0=$SECONDS
    image="$(KREMA_DOCKER_PLATFORM="$platform" \
        "$script_dir/build-ci-image.sh" --pull builder "$id")" || return $?
    echo "[distro] builder-image: $((SECONDS - t0))s"

    local target_out="$outdir/$id"
    local ccache_dir="${KREMA_CCACHE_DIR:-$script_dir/.cache/ccache/$id}"
    mkdir -p "$target_out" "$ccache_dir" || return $?

    local args=(
        run --rm --init
        -v "$stage_dir:/stage:ro"
        -v "$packaging_dir:/pkg/packaging:ro"
        -v "$script_dir/pkg:/pkg/bin:ro"
        -v "$target_out:/out"
        -v "$(cd -- "$ccache_dir" && pwd):/ccache"
        -e "TARGET_ID=$id"
        -e "FAMILY=$family"
        -e "KREMA_VERSION=$version"
        -e "HOST_UID=$(id -u)"
        -e "HOST_GID=$(id -g)"
    )
    [[ -z "${CCACHE_MAXSIZE:-}" ]] || args+=(-e "CCACHE_MAXSIZE=$CCACHE_MAXSIZE")
    [[ -n "$platform" ]] && args+=(--platform "$platform")
    args+=("$image" "/pkg/bin/in-container.sh")

    docker "${args[@]}"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

targets=()
if [[ "$target_filter" == "all" ]]; then
    mapfile -t targets < <(distro_targets)
else
    targets=("$target_filter")
fi

declare -A results=()
overall_start=$SECONDS

for target in "${targets[@]}"; do
    echo "=== $target ==="
    if ! distro_target "$target"; then
        echo "error: unknown target '$target' (not in tests/distro/targets.tsv," \
            "and not 'arch')" >&2
        results[$target]="unknown-target"
        continue
    fi
    family="$DISTRO_FAMILY"

    if ! version="$(package_version "$family")" || [[ -z "$version" ]]; then
        echo "error: cannot determine package version for family '$family'" >&2
        results[$target]="no-version"
        continue
    fi

    # mktemp dirs are 0700; the arch build drops privileges to a builder user.
    stage_dir="$(mktemp -d)"
    trap 'rm -rf "$stage_dir"' EXIT
    chmod 755 "$stage_dir"
    make_source_tarball "$version" "$stage_dir"
    echo "source: krema-$version.tar.gz  base: $DISTRO_BASE_IMAGE"

    start=$SECONDS
    if run_build "$target" "$family"; then
        results[$target]="ok ($((SECONDS - start))s)"
    else
        results[$target]="FAILED (rc=$?)"
    fi
    rm -rf "$stage_dir"
done

echo
echo "=== Package summary ==="
status=0
for target in "${targets[@]}"; do
    printf '%-22s %s\n' "$target" "${results[$target]:-skipped}"
    [[ "${results[$target]:-}" == ok* ]] || status=1
done
echo "total: $((SECONDS - overall_start))s"

find "$outdir" -type f \( -name '*.rpm' -o -name '*.deb' \
    -o -name '*.pkg.tar.zst' \) -printf '%10s  %p\n' 2>/dev/null | sort -k2

exit "$status"
