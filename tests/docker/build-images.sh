#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Build runtime smoke images locally from the digest-pinned base recorded in
# targets.tsv. Each target's base image is passed to Dockerfile.runtime as
# `BASE_IMAGE=docker.io/<image>@<sha256:digest>` so rebuilds against the
# same locked targets.tsv row are reproducible.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
targets_file="$script_dir/targets.tsv"
image_prefix="${KREMA_DOCKER_IMAGE_PREFIX:-krema/gui-runtime}"
docker_host="${DOCKER_HOST:-tcp://localhost:2375}"
platform="${KREMA_DOCKER_PLATFORM:-}"
target_filter="${1:-all}"

usage() {
    cat <<'EOF'
Usage: tests/docker/build-images.sh [target-id|all]

Builds runtime smoke images locally using the sha256 base_digest from
targets.tsv. Run tests/docker/update-digests.sh first if any row still has
a `pending` base_digest value.

Environment:
  DOCKER_HOST                 Docker daemon endpoint (default: tcp://localhost:2375)
  KREMA_DOCKER_IMAGE_PREFIX   Image prefix (default: krema/gui-runtime)
  KREMA_DOCKER_PLATFORM       Optional Docker platform, for example linux/amd64 or linux/arm64
EOF
}

if [[ "$target_filter" == "--help" || "$target_filter" == "-h" ]]; then
    usage
    exit 0
fi

build_target() {
    local target_id="$1"
    local family="$2"
    local base_image="$3"
    local base_digest="$4"

    if [[ "$base_digest" != sha256:* ]]; then
        echo "ERROR: $target_id has unresolved base_digest ('$base_digest')." >&2
        echo "  Run tests/docker/update-digests.sh $target_id to lock it." >&2
        exit 65
    fi

    local pinned_base="docker.io/${base_image}@${base_digest}"
    local tag="$image_prefix:$target_id"
    local args=(build -f "$script_dir/Dockerfile.runtime" -t "$tag"
        --build-arg "BASE_IMAGE=$pinned_base"
        --build-arg "TARGET_ID=$target_id"
        --build-arg "FAMILY=$family")
    if [[ -n "$platform" ]]; then
        args+=(--platform "$platform")
    fi
    args+=("$script_dir")

    echo "Building $tag from $pinned_base"
    DOCKER_HOST="$docker_host" docker "${args[@]}"
}

matched=0
while IFS= read -r raw_line || [[ -n "$raw_line" ]]; do
    if [[ -z "${raw_line//[[:space:]]/}" || "${raw_line:0:1}" == "#" ]]; then
        continue
    fi
    IFS=$'\t' read -r target_id family base_image base_digest obs_repository package_glob <<<"$raw_line"
    if [[ -z "${target_id:-}" || -z "${family:-}" || -z "${base_image:-}" \
          || -z "${base_digest:-}" || -z "${obs_repository:-}" \
          || -z "${package_glob:-}" ]]; then
        echo "ERROR: malformed targets.tsv row (need 6 TAB-separated columns):" >&2
        printf '  %s\n' "$raw_line" >&2
        exit 65
    fi
    if [[ "$target_filter" != "all" && "$target_filter" != "$target_id" ]]; then
        continue
    fi
    matched=1
    build_target "$target_id" "$family" "$base_image" "$base_digest"
done <"$targets_file"

if ((matched == 0)); then
    echo "No target matched: $target_filter" >&2
    exit 64
fi
