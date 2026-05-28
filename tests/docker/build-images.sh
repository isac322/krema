#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

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

    local tag="$image_prefix:$target_id"
    local args=(build --pull -f "$script_dir/Dockerfile.runtime" -t "$tag" --build-arg "BASE_IMAGE=$base_image" --build-arg "TARGET_ID=$target_id" --build-arg "FAMILY=$family")
    if [[ -n "$platform" ]]; then
        args+=(--platform "$platform")
    fi
    args+=("$script_dir")

    echo "Building $tag from $base_image"
    DOCKER_HOST="$docker_host" docker "${args[@]}"
}

matched=0
while IFS=$'\t' read -r target_id family base_image obs_repository package_glob; do
    [[ -z "${target_id:-}" || "${target_id:0:1}" == "#" ]] && continue
    if [[ "$target_filter" != "all" && "$target_filter" != "$target_id" ]]; then
        continue
    fi
    matched=1
    build_target "$target_id" "$family" "$base_image"
done <"$targets_file"

if ((matched == 0)); then
    echo "No target matched: $target_filter" >&2
    exit 64
fi
