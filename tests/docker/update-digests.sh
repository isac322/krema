#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Refresh the base_digest column of targets.tsv by resolving the current
# sha256 manifest digest of each base_image via
# `docker buildx imagetools inspect`.
#
# Targets file format (TAB-separated):
#   target_id<TAB>family<TAB>base_image<TAB>base_digest<TAB>obs_repository<TAB>package_glob
#
# The base_digest column is the multi-arch manifest list digest even when
# the runtime image is later built single-platform (for example Slowroll
# is amd64-only). The single-platform constraint is enforced by the
# publish workflow, not by the lockfile, so multiple target rows can share
# the same base_image and therefore the same base_digest.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
targets_file="$script_dir/targets.tsv"
docker_host="${DOCKER_HOST:-tcp://localhost:2375}"

usage() {
    cat <<'EOF'
Usage: tests/docker/update-digests.sh [target-id|all]

Resolves the current sha256 manifest digest of each target's base_image
via `docker buildx imagetools inspect` and rewrites targets.tsv in place.

Environment:
  DOCKER_HOST   Docker daemon endpoint (default: tcp://localhost:2375)

Notes:
  - targets.tsv must be in 6-column format
    (target_id, family, base_image, base_digest, obs_repository, package_glob).
  - Use the literal value `pending` (or any non-sha256: string) in the
    base_digest column for rows that have not been locked yet.
  - Comments (#) and blank lines pass through unchanged. Data row order is
    preserved.
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    usage
    exit 0
fi

target_filter="${1:-all}"

if [[ ! -f "$targets_file" ]]; then
    echo "targets file not found: $targets_file" >&2
    exit 66
fi

tmp_file="$(mktemp)"
trap 'rm -f "$tmp_file"' EXIT

matched=0
while IFS= read -r raw_line || [[ -n "$raw_line" ]]; do
    if [[ -z "${raw_line//[[:space:]]/}" || "${raw_line:0:1}" == "#" ]]; then
        printf '%s\n' "$raw_line" >>"$tmp_file"
        continue
    fi

    IFS=$'\t' read -r target_id family base_image base_digest obs_repository package_glob <<<"$raw_line"

    if [[ -z "${target_id:-}" || -z "${family:-}" || -z "${base_image:-}" \
          || -z "${obs_repository:-}" || -z "${package_glob:-}" ]]; then
        echo "ERROR: malformed row (need 6 TAB-separated columns):" >&2
        printf '  %s\n' "$raw_line" >&2
        exit 65
    fi

    if [[ "$target_filter" != "all" && "$target_filter" != "$target_id" ]]; then
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$target_id" "$family" "$base_image" "$base_digest" \
            "$obs_repository" "$package_glob" >>"$tmp_file"
        continue
    fi

    matched=1
    echo "Resolving $target_id (base $base_image)..." >&2

    if ! resolved="$(
            DOCKER_HOST="$docker_host" docker buildx imagetools inspect \
                "docker.io/$base_image" --format '{{.Manifest.Digest}}' 2>&1
        )"; then
        echo "ERROR: failed to inspect docker.io/$base_image" >&2
        printf '  %s\n' "$resolved" >&2
        exit 67
    fi

    if [[ "$resolved" != sha256:* ]]; then
        echo "ERROR: unexpected digest format for $base_image: '$resolved'" >&2
        exit 67
    fi

    printf '  %s -> %s\n' "$base_digest" "$resolved" >&2

    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$target_id" "$family" "$base_image" "$resolved" \
        "$obs_repository" "$package_glob" >>"$tmp_file"
done <"$targets_file"

if ((matched == 0)); then
    echo "No target matched: $target_filter" >&2
    exit 64
fi

mv "$tmp_file" "$targets_file"
trap - EXIT
echo "Updated $targets_file" >&2
