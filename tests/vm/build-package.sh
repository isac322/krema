#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Build an installable Krema package for a Tier-3 VM target inside a clean
# container of that target's base image, using the repo's own packaging files.
#
#   tests/vm/build-package.sh <target-id> <outdir>
#   tests/vm/build-package.sh all <outdir>
#
# <target-id> is a row in tests/docker/targets.tsv, or `arch` which builds
# packaging/arch/PKGBUILD in an archlinux:latest container (amd64 image only).
#
# The package is built from the CURRENT source tree (the worktree minus VCS
# and build-output directories), packed as krema-<version>.tar.gz with the
# same root directory layout as the GitHub release tarball that OBS consumes
# via packaging/obs/_service (tar_scm). The build inside the container needs
# no network other than the distro package repositories.
#
# Output: <outdir>/<target-id>/ contains the produced .rpm / .deb /
# .pkg.tar.zst binary package.
#
# Environment:
#   DOCKER_HOST            docker daemon to use (default: ambient socket)
#   KREMA_DOCKER_PLATFORM  force container platform, e.g. linux/amd64
#   KREMA_ARCH_IMAGE       arch base image (default: docker.io/archlinux:latest)

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"
targets_file="$repo_root/tests/docker/targets.tsv"
packaging_dir="$repo_root/packaging"

usage() {
    sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# \?//' >&2
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
# Target table
# ---------------------------------------------------------------------------

# known_target <id> -> prints "family<TAB>image-ref"
known_target() {
    local id="$1"
    if [[ "$id" == "arch" ]]; then
        printf 'arch\t%s\n' "${KREMA_ARCH_IMAGE:-docker.io/archlinux:latest}"
        return 0
    fi
    awk -F'\t' -v id="$id" '
        /^[[:space:]]*#/ || NF < 4 { next }
        $1 == id {
            image = "docker.io/" $3
            if ($4 != "" && $4 != "pending") image = image "@" $4
            printf "%s\t%s\n", $2, image
            found = 1
        }
        END { exit(found ? 0 : 1) }
    ' "$targets_file"
}

all_targets() {
    awk -F'\t' '/^[[:space:]]*#/ || NF < 4 { next } { print $1 }' "$targets_file"
    echo arch
}

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
        # VM QA cache (cloud/golden qcow2 images, GBs) and run artifacts.
        --exclude='./tests/vm/.cache' --exclude='./tests/vm/artifacts'
    )
    # Never package our own output directory if the caller put <outdir> inside
    # the repo.
    local rel
    if rel="$(realpath --relative-to="$repo_root" "$outdir" 2>/dev/null)" \
        && [[ "$rel" != ..* && "$rel" != "." ]]; then
        excludes+=(--exclude="./$rel")
    fi

    "$tar_cmd" --create --gzip --file="$dest/krema-$version.tar.gz" \
        --sort=name --numeric-owner --owner=0 --group=0 \
        --mtime=@1700000000 \
        "${excludes[@]}" \
        --transform="s,^\\./,krema-$version/," \
        -C "$repo_root" .
}

# ---------------------------------------------------------------------------
# Container run
# ---------------------------------------------------------------------------

# amd64_only_target <id>: targets whose distro repositories publish x86_64
# packages only, so the build container must run linux/amd64 (Slowroll's OBS
# matrix is amd64-only; the official archlinux image is amd64-only).
amd64_only_target() {
    [[ "$1" == "arch" || "$1" == "opensuse-slowroll" ]]
}

# run_build <target-id> <family> <image-ref> -> docker run of pkg/in-container.sh
run_build() {
    local id="$1" family="$2" image="$3"
    local platform="${KREMA_DOCKER_PLATFORM:-}"

    if [[ -z "$platform" ]] && amd64_only_target "$id"; then
        platform="linux/amd64"
        if [[ "$host_arch" != "x86_64" ]] \
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

    local target_out="$outdir/$id"
    mkdir -p "$target_out"

    local args=(
        run --rm --init
        -v "$stage_dir:/stage:ro"
        -v "$packaging_dir:/pkg/packaging:ro"
        -v "$script_dir/pkg:/pkg/bin:ro"
        -v "$target_out:/out"
        -e "TARGET_ID=$id"
        -e "FAMILY=$family"
        -e "KREMA_VERSION=$version"
    )
    [[ -n "$platform" ]] && args+=(--platform "$platform")
    args+=("$image" "/pkg/bin/in-container.sh")

    docker "${args[@]}"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

targets=()
if [[ "$target_filter" == "all" ]]; then
    mapfile -t targets < <(all_targets)
else
    targets=("$target_filter")
fi

declare -A results=()
overall_start=$SECONDS

for target in "${targets[@]}"; do
    echo "=== $target ==="
    if ! target_row="$(known_target "$target")"; then
        echo "error: unknown target '$target' (not in tests/docker/targets.tsv," \
            "and not 'arch')" >&2
        results[$target]="unknown-target"
        continue
    fi
    family="${target_row%%$'\t'*}"
    image="${target_row#*$'\t'}"

    if ! version="$(package_version "$family")" || [[ -z "$version" ]]; then
        echo "error: cannot determine package version for family '$family'" >&2
        results[$target]="no-version"
        continue
    fi

    # mktemp dirs are 0700; the arch build drops privileges to a builder user.
    stage_dir="$(mktemp -d)"
    chmod 755 "$stage_dir"
    make_source_tarball "$version" "$stage_dir"
    echo "source: krema-$version.tar.gz  image: $image"

    start=$SECONDS
    if run_build "$target" "$family" "$image"; then
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

find "$outdir" -type f \( -name '*.rpm' -o -name '*.deb' -o -name '*.ddeb' \
    -o -name '*.pkg.tar.zst' \) -printf '%10s  %p\n' 2>/dev/null | sort -k2

exit "$status"
