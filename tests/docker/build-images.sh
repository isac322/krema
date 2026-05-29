#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Build runtime smoke images locally from the digest-pinned base recorded in
# targets.tsv. Each target's base image is passed to Dockerfile.runtime as
# `BASE_IMAGE=docker.io/<image>@<sha256:digest>` so rebuilds against the
# same locked targets.tsv row are reproducible.
#
# When multiple targets are selected, builds run in parallel (default 4
# concurrent jobs). Each target's docker build output is streamed to a
# per-target log file and a pass/fail summary is printed at the end.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
targets_file="$script_dir/targets.tsv"
image_prefix="${KREMA_DOCKER_IMAGE_PREFIX:-krema/gui-runtime}"
docker_host="${DOCKER_HOST:-tcp://localhost:2375}"
platform="${KREMA_DOCKER_PLATFORM:-}"
parallel="${KREMA_DOCKER_PARALLEL:-4}"
log_dir="${KREMA_DOCKER_LOG_DIR:-${TMPDIR:-/tmp}/krema-build-images}"
target_filter="${1:-all}"

usage() {
    cat <<'EOF'
Usage: tests/docker/build-images.sh [target-id|all]

Builds runtime smoke images locally using the sha256 base_digest from
targets.tsv. Run tests/docker/update-digests.sh first if any row still has
a `pending` base_digest value.

Multiple targets build in parallel by default (4 concurrent jobs). Each
target's output is captured to $KREMA_DOCKER_LOG_DIR/<target>.log.

Environment:
  DOCKER_HOST                 Docker daemon endpoint (default: tcp://localhost:2375)
  KREMA_DOCKER_IMAGE_PREFIX   Image prefix (default: krema/gui-runtime)
  KREMA_DOCKER_PLATFORM       Optional Docker platform, for example linux/amd64 or linux/arm64
  KREMA_DOCKER_PARALLEL       Max concurrent target builds (default: 4). Set to 1
                              to stream output sequentially like the old behaviour.
                              Values >1 require bash 5.1+.
  KREMA_DOCKER_LOG_DIR        Per-target log directory when parallel>1
                              (default: ${TMPDIR:-/tmp}/krema-build-images)
EOF
}

if [[ "$target_filter" == "--help" || "$target_filter" == "-h" ]]; then
    usage
    exit 0
fi

if ! [[ "$parallel" =~ ^[1-9][0-9]*$ ]]; then
    echo "ERROR: KREMA_DOCKER_PARALLEL must be a positive integer (got '$parallel')" >&2
    exit 64
fi

build_target() {
    local target_id="$1"
    local family="$2"
    local base_image="$3"
    local base_digest="$4"

    if [[ "$base_digest" != sha256:* ]]; then
        echo "ERROR: $target_id has unresolved base_digest ('$base_digest')." >&2
        echo "  Run tests/docker/update-digests.sh $target_id to lock it." >&2
        return 65
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

selected_lines=()
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
    selected_lines+=("$target_id"$'\t'"$family"$'\t'"$base_image"$'\t'"$base_digest")
done <"$targets_file"

if ((${#selected_lines[@]} == 0)); then
    echo "No target matched: $target_filter" >&2
    exit 64
fi

ok_list=()
fail_list=()

# Serial / streaming mode: single target OR parallel=1.
if ((${#selected_lines[@]} == 1 || parallel == 1)); then
    for tuple in "${selected_lines[@]}"; do
        IFS=$'\t' read -r t_id t_family t_base t_digest <<<"$tuple"
        if build_target "$t_id" "$t_family" "$t_base" "$t_digest"; then
            ok_list+=("$t_id")
        else
            rc=$?
            echo "FAIL $t_id (exit $rc)" >&2
            fail_list+=("$t_id")
        fi
    done
else
    if ((BASH_VERSINFO[0] < 5)) || ((BASH_VERSINFO[0] == 5 && BASH_VERSINFO[1] < 1)); then
        echo "ERROR: KREMA_DOCKER_PARALLEL=$parallel requires bash 5.1+ (have ${BASH_VERSION})." >&2
        echo "  Set KREMA_DOCKER_PARALLEL=1 or upgrade bash." >&2
        exit 65
    fi

    mkdir -p "$log_dir"
    echo "Parallel build: up to $parallel concurrent, ${#selected_lines[@]} targets"
    echo "Logs: $log_dir/<target>.log"
    echo

    declare -A pid_to_target
    declare -A pid_to_log
    queue=("${selected_lines[@]}")
    running=0

    start_next() {
        ((${#queue[@]} == 0)) && return 1
        local tuple="${queue[0]}"
        queue=("${queue[@]:1}")
        local t_id t_family t_base t_digest
        IFS=$'\t' read -r t_id t_family t_base t_digest <<<"$tuple"
        local log_file="$log_dir/$t_id.log"
        echo "[start] $t_id  →  $log_file"
        (build_target "$t_id" "$t_family" "$t_base" "$t_digest") \
            >"$log_file" 2>&1 &
        local p=$!
        pid_to_target[$p]="$t_id"
        pid_to_log[$p]="$log_file"
        running=$((running + 1))
        return 0
    }

    for ((i = 0; i < parallel; i++)); do
        start_next || break
    done

    finished_pid=""
    while ((running > 0)); do
        finished_pid=""
        rc=0
        wait -n -p finished_pid || rc=$?
        running=$((running - 1))
        if [[ -z "$finished_pid" ]]; then
            echo "[warn] wait -n returned without a pid" >&2
            continue
        fi
        tid="${pid_to_target[$finished_pid]:-unknown}"
        logf="${pid_to_log[$finished_pid]:-}"
        unset 'pid_to_target[$finished_pid]'
        unset 'pid_to_log[$finished_pid]'
        if ((rc == 0)); then
            echo "[ok]    $tid"
            ok_list+=("$tid")
        else
            echo "[fail]  $tid (exit $rc, see $logf)"
            fail_list+=("$tid")
        fi
        start_next || true
    done
fi

echo
echo "=== Build summary ==="
if ((${#ok_list[@]} > 0)); then
    echo "Pass (${#ok_list[@]}): ${ok_list[*]}"
else
    echo "Pass (0): -"
fi
if ((${#fail_list[@]} > 0)); then
    echo "Fail (${#fail_list[@]}): ${fail_list[*]}" >&2
    exit 1
fi
