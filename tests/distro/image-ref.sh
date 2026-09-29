#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Print the content-addressed reference of a Krema CI image.
#
#   tests/distro/image-ref.sh runtime <target>   distro E2E runtime (no krema)
#   tests/distro/image-ref.sh builder <target>   distro package builder
#   tests/distro/image-ref.sh ctest              tests/appium/Dockerfile ctest-image
#   tests/distro/image-ref.sh vgem               FROM scratch image with /vgem.ko
#
# <target> is a row of tests/distro/targets.tsv or `arch`. The reference is
#
#   <registry>:runtime-<target>-<hash12>
#   <registry>:builder-<target>-<hash12>
#   <registry>:ctest-<base>-<hash12>          (<base>: fedora-43 for fedora:43)
#   <registry>:vgem-<kernel release>-<hash12>
#
# <hash12> is the first 12 hex digits of a sha256 over the files that define
# the image (paths and contents) and the build arguments:
#
#   runtime, builder  tests/distro/image/{Dockerfile,packages.sh,<family>.sh},
#                     every file under tests/appium/tools/ (the swas patches
#                     and krema-test-window the Dockerfile copies; the whole
#                     directory, so a new patch cannot be missed), and the target's
#                     id, family and base image reference (with its pinned
#                     digest). Both kinds of a target share one hash, so they
#                     are always published, missing or rebuilt together:
#                     a package is never built on a builder whose distro
#                     snapshot differs from the runtime it is installed into.
#                     packaging/ is deliberately NOT an input: the builder
#                     installs the missing build deps at job time (see
#                     packages.sh builddeps), so packaging changes never force
#                     an image rebuild.
#   ctest             tests/appium/Dockerfile (its ctest-image target copies
#                     no file from the build context)
#   vgem              tests/appium/setup-vgem.sh; the kernel release is part of
#                     the tag, not the hash
#
# The hash does not change when distro repositories move on: freshness is the
# publisher's job (.github/workflows/ci-images.yml rebuilds and re-pushes).
#
# Environment:
#   KREMA_CI_REGISTRY   image repository (default ghcr.io/isac322/krema-ci)
#   KREMA_VGEM_KERNEL   kernel release for the vgem kind (default: uname -r)
#   KREMA_ARCH_IMAGE    arch base image (default docker.io/archlinux:latest)
#
# Sourced (by build-ci-image.sh, build-package.sh, run-distro-e2e.sh), it
# only defines the distro_* and ci_image_ref functions. It reads
# targets.tsv only for the runtime and builder kinds.

_ir_here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
_ir_repo="$(cd -- "$_ir_here/../.." && pwd)"

# distro_target <id>: sets DISTRO_FAMILY and DISTRO_BASE_IMAGE; returns 1 for
# an unknown target.
distro_target() {
    local id="$1" row
    if [[ "$id" == arch ]]; then
        DISTRO_FAMILY=arch
        DISTRO_BASE_IMAGE="${KREMA_ARCH_IMAGE:-docker.io/archlinux:latest}"
        return 0
    fi
    row="$(awk -F'\t' -v id="$id" '
        /^[[:space:]]*#/ || NF < 4 { next }
        $1 == id {
            image = "docker.io/" $3
            if ($4 != "" && $4 != "pending") image = image "@" $4
            print $2 "\t" image
            found = 1
        }
        END { exit(found ? 0 : 1) }
    ' "$_ir_here/targets.tsv")" || return 1
    DISTRO_FAMILY="${row%%$'\t'*}"
    DISTRO_BASE_IMAGE="${row#*$'\t'}"
}

# distro_targets: every target id, one per line.
distro_targets() {
    awk -F'\t' '/^[[:space:]]*#/ || NF < 4 { next } { print $1 }' "$_ir_here/targets.tsv"
    echo arch
}

# distro_platform <id>: the container platform a target needs, empty for
# native. arch and opensuse-slowroll repositories publish x86_64 only.
distro_platform() {
    case "$1" in
        arch | opensuse-slowroll) echo linux/amd64 ;;
        *) echo ;;
    esac
}

_ir_sha256() {
    if command -v sha256sum >/dev/null; then
        sha256sum | cut -c1-64
    else
        shasum -a 256 | cut -c1-64
    fi
}

# _ir_hash <key=value>... -- <repo-relative file>...: 12-hex digest over the
# key/value lines, then one "<path> <sha256 of content>" line per file.
_ir_hash() {
    local f
    {
        while (($#)) && [[ "$1" != -- ]]; do
            printf '%s\n' "$1"
            shift
        done
        shift
        for f in "$@"; do
            [[ -f "$_ir_repo/$f" ]] || { echo "image-ref: missing input $f" >&2; return 1; }
            printf '%s %s\n' "$f" "$(_ir_sha256 <"$_ir_repo/$f")"
        done
    } | _ir_sha256 | cut -c1-12
}

# ci_image_ref <kind> [<target>]: prints the reference; returns 64 on bad
# arguments.
ci_image_ref() {
    local kind="${1:-}" target="${2:-}" registry="${KREMA_CI_REGISTRY:-ghcr.io/isac322/krema-ci}"
    local hash
    case "$kind" in
        runtime | builder)
            [[ -n "$target" ]] || { echo "image-ref: $kind needs a target" >&2; return 64; }
            distro_target "$target" || {
                echo "image-ref: unknown target '$target' (not in tests/distro/targets.tsv, and not 'arch')" >&2
                return 64
            }
            local -a files=(
                tests/distro/image/Dockerfile
                tests/distro/image/packages.sh
                "tests/distro/image/$DISTRO_FAMILY.sh"
            )
            local f
            while IFS= read -r f; do
                files+=("${f#"$_ir_repo"/}")
            done < <(find "$_ir_repo/tests/appium/tools" -type f | LC_ALL=C sort)
            hash="$(_ir_hash "target=$target" "family=$DISTRO_FAMILY" "base=$DISTRO_BASE_IMAGE" \
                -- "${files[@]}")" || return 1
            printf '%s:%s-%s-%s\n' "$registry" "$kind" "$target" "$hash"
            ;;
        ctest)
            [[ -z "$target" ]] || { echo "image-ref: ctest takes no target" >&2; return 64; }
            local base
            base="$(sed -n 's/^ARG BASE_IMAGE=//p' "$_ir_repo/tests/appium/Dockerfile" | head -n1)"
            [[ -n "$base" ]] || { echo "image-ref: no ARG BASE_IMAGE in tests/appium/Dockerfile" >&2; return 1; }
            hash="$(_ir_hash "target=ctest-image" -- tests/appium/Dockerfile)" || return 1
            printf '%s:ctest-%s-%s\n' "$registry" "${base//[^A-Za-z0-9_.-]/-}" "$hash"
            ;;
        vgem)
            [[ -z "$target" ]] || { echo "image-ref: vgem takes no target" >&2; return 64; }
            local krel="${KREMA_VGEM_KERNEL:-$(uname -r)}"
            hash="$(_ir_hash -- tests/appium/setup-vgem.sh)" || return 1
            printf '%s:vgem-%s-%s\n' "$registry" "${krel//[^A-Za-z0-9_.-]/_}" "$hash"
            ;;
        *)
            echo "usage: tests/distro/image-ref.sh runtime|builder <target> | ctest | vgem" >&2
            return 64
            ;;
    esac
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    set -euo pipefail
    (($# >= 1 && $# <= 2)) || { ci_image_ref; exit 64; }
    ci_image_ref "$@"
fi
