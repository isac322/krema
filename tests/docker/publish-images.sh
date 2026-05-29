#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Build per-distro runtime smoke images and push them to GitHub Container
# Registry as OCI manifests. Inputs:
#
#   - tests/docker/targets.tsv          (digest-pinned base per target)
#   - tests/docker/Dockerfile.runtime
#   - tests/docker/container-smoke.sh   (copied into the image)
#
# Image tag scheme (per-target repo):
#   ghcr.io/<owner>/<repo-prefix>-<target_id>:<YYYYMMDD>-<git-sha7>  ← immutable
#   ghcr.io/<owner>/<repo-prefix>-<target_id>:latest                 ← convenience
#
# :latest is pushed only when the current branch is master AND the working
# tree is clean (override with KREMA_DOCKER_PUSH_LATEST=1 or =0).
#
# opensuse-slowroll has amd64-only packages and is published as a single
# linux/amd64 manifest. Every other target is linux/amd64,linux/arm64.
#
# When multiple targets are selected, builds run in parallel (default 4
# concurrent jobs). Each target's buildx output streams to a per-target log
# file and a pass/fail summary is printed at the end.
#
# Reproducibility levers:
#   - BASE_IMAGE is pinned to docker.io/<image>@<sha256:digest> from the
#     lockfile (refresh via tests/docker/update-digests.sh).
#   - SOURCE_DATE_EPOCH is set to the commit timestamp of the latest input
#     change so layer mtimes are normalised.
#   - --provenance=false --sbom=false suppress the extra attestation
#     manifests buildx attaches by default.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"
targets_file="$script_dir/targets.tsv"

docker_host="${DOCKER_HOST:-tcp://localhost:2375}"
ghcr_owner="${KREMA_GHCR_OWNER:-isac322}"
ghcr_repo_prefix="${KREMA_GHCR_REPO_PREFIX:-krema-gui-runtime}"
builder="${KREMA_DOCKER_BUILDER:-krema-multiarch}"
dry_run="${KREMA_DOCKER_DRY_RUN:-0}"
push_latest_override="${KREMA_DOCKER_PUSH_LATEST:-}"
parallel="${KREMA_DOCKER_PARALLEL:-4}"
log_dir="${KREMA_DOCKER_LOG_DIR:-${TMPDIR:-/tmp}/krema-publish-images}"
target_filter="${1:-all}"

usage() {
    cat <<'EOF'
Usage: tests/docker/publish-images.sh [target-id|all]

Builds runtime smoke images with `docker buildx` from the digest-pinned
base recorded in targets.tsv and pushes them to GHCR as OCI manifests.
Slowroll is published linux/amd64-only; every other target is
linux/amd64,linux/arm64.

Multiple targets build in parallel by default (4 concurrent jobs). Each
target's output is captured to $KREMA_DOCKER_LOG_DIR/<target>.log.

Authentication is the caller's responsibility, for example:
  gh auth token | docker login ghcr.io -u <user> --password-stdin

Environment:
  DOCKER_HOST                Docker daemon endpoint (default: tcp://localhost:2375)
  KREMA_GHCR_OWNER           ghcr owner (default: isac322)
  KREMA_GHCR_REPO_PREFIX     repo prefix (default: krema-gui-runtime)
  KREMA_DOCKER_BUILDER       buildx builder name (default: krema-multiarch).
                             Auto-created with `--driver docker-container --bootstrap`
                             on first run if missing.
  KREMA_DOCKER_DRY_RUN       If 1, omit --push and only print the resolved
                             buildx command and tags. Default: 0.
  KREMA_DOCKER_PUSH_LATEST   Override the auto-detected :latest policy:
                             1 → always push :latest, 0 → never push :latest.
                             Unset → push :latest only on a clean master tree.
  KREMA_DOCKER_PARALLEL      Max concurrent target builds (default: 4). Set to 1
                             to stream output sequentially like the old behaviour.
                             Values >1 require bash 5.1+.
  KREMA_DOCKER_LOG_DIR       Per-target log directory when parallel>1
                             (default: ${TMPDIR:-/tmp}/krema-publish-images)

Lockfile prerequisites:
  - Every targets.tsv row must have a sha256: base_digest.
    Refresh via: tests/docker/update-digests.sh [target|all]

Multi-arch prerequisites:
  - For multi-arch targets the host must have QEMU binfmt handlers for the
    non-native architecture. Install on Arch with:
      sudo pacman -S qemu-user-static qemu-user-static-binfmt
      sudo systemctl restart systemd-binfmt
EOF
}

if [[ "$target_filter" == "--help" || "$target_filter" == "-h" ]]; then
    usage
    exit 0
fi

if ! command -v git >/dev/null 2>&1; then
    echo "ERROR: git is required" >&2
    exit 65
fi

if ! [[ "$parallel" =~ ^[1-9][0-9]*$ ]]; then
    echo "ERROR: KREMA_DOCKER_PARALLEL must be a positive integer (got '$parallel')" >&2
    exit 64
fi

cd "$repo_root"

git_sha="$(git rev-parse HEAD)"
git_sha7="$(git rev-parse --short=7 HEAD)"
date_tag="$(date -u +%Y%m%d)"
immutable_suffix="${date_tag}-${git_sha7}"

# Pin SOURCE_DATE_EPOCH to the latest commit that touched any build input.
src_files=(
    tests/docker/Dockerfile.runtime
    tests/docker/container-smoke.sh
    tests/docker/targets.tsv
)
src_epoch="$(git log -1 --format=%ct -- "${src_files[@]}")"
if [[ -z "$src_epoch" ]]; then
    src_epoch="$(git log -1 --format=%ct HEAD)"
fi
src_iso="$(date -u -d "@$src_epoch" +%Y-%m-%dT%H:%M:%SZ)"

# Decide whether to push :latest.
current_branch="$(git rev-parse --abbrev-ref HEAD)"
tree_dirty="$(git status --porcelain | wc -l)"
case "$push_latest_override" in
    1)   push_latest=1 ;;
    0)   push_latest=0 ;;
    "")  if [[ "$current_branch" == "master" && "$tree_dirty" -eq 0 ]]; then
             push_latest=1
         else
             push_latest=0
         fi ;;
    *)   echo "ERROR: KREMA_DOCKER_PUSH_LATEST must be 0 or 1 (got '$push_latest_override')" >&2
         exit 64 ;;
esac

ensure_builder() {
    if DOCKER_HOST="$docker_host" docker buildx inspect "$builder" >/dev/null 2>&1; then
        return 0
    fi
    echo "Creating buildx builder: $builder" >&2
    DOCKER_HOST="$docker_host" docker buildx create \
        --name "$builder" --driver docker-container --bootstrap >/dev/null
}

publish_target() {
    local target_id="$1"
    local family="$2"
    local base_image="$3"
    local base_digest="$4"

    if [[ "$base_digest" != sha256:* ]]; then
        echo "ERROR: $target_id has unresolved base_digest ('$base_digest')." >&2
        echo "  Run tests/docker/update-digests.sh $target_id to lock it." >&2
        return 65
    fi

    local platforms="linux/amd64,linux/arm64"
    if [[ "$target_id" == "opensuse-slowroll" ]]; then
        platforms="linux/amd64"
    fi

    local pinned_base="docker.io/${base_image}@${base_digest}"
    local repo="ghcr.io/${ghcr_owner}/${ghcr_repo_prefix}-${target_id}"
    local immutable_tag="${repo}:${immutable_suffix}"
    local latest_tag="${repo}:latest"

    local args=(
        buildx build
        --builder "$builder"
        --platform "$platforms"
        --provenance=false
        --sbom=false
        --build-arg "BASE_IMAGE=$pinned_base"
        --build-arg "TARGET_ID=$target_id"
        --build-arg "FAMILY=$family"
        --build-arg "SOURCE_DATE_EPOCH=$src_epoch"
        --label "org.opencontainers.image.revision=$git_sha"
        --label "org.opencontainers.image.created=$src_iso"
        --label "org.opencontainers.image.version=$immutable_suffix"
        --label "org.opencontainers.image.source=https://github.com/isac322/krema"
        --label "org.opencontainers.image.url=https://github.com/isac322/krema"
        --label "org.opencontainers.image.documentation=https://github.com/isac322/krema/blob/master/tests/docker/README.md"
        --label "org.opencontainers.image.licenses=GPL-3.0-or-later"
        --label "org.opencontainers.image.base.name=docker.io/${base_image}"
        --label "org.opencontainers.image.base.digest=$base_digest"
        --label "com.bhyoo.krema.target=$target_id"
        --label "com.bhyoo.krema.family=$family"
        --tag "$immutable_tag"
    )
    if ((push_latest == 1)); then
        args+=(--tag "$latest_tag")
    fi

    if ((dry_run == 1)); then
        echo "[DRY RUN] $immutable_tag  (platforms: $platforms)"
        if ((push_latest == 1)); then
            echo "[DRY RUN] $latest_tag  (also pushed)"
        else
            echo "[DRY RUN] (:latest not pushed; branch=$current_branch, dirty=$tree_dirty, override=${push_latest_override:-auto})"
        fi
        echo "[DRY RUN] command: docker ${args[*]} --file $script_dir/Dockerfile.runtime $script_dir"
        return 0
    fi

    args+=(--push --file "$script_dir/Dockerfile.runtime" "$script_dir")

    echo "Publishing $immutable_tag (platforms: $platforms)"
    if ((push_latest == 1)); then
        echo "             + $latest_tag"
    fi
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

if ((dry_run == 0)); then
    ensure_builder
fi

ok_list=()
fail_list=()

# Serial / streaming mode: single target OR parallel=1 OR dry-run.
if ((${#selected_lines[@]} == 1 || parallel == 1 || dry_run == 1)); then
    for tuple in "${selected_lines[@]}"; do
        IFS=$'\t' read -r t_id t_family t_base t_digest <<<"$tuple"
        if publish_target "$t_id" "$t_family" "$t_base" "$t_digest"; then
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
    echo "Parallel publish: up to $parallel concurrent, ${#selected_lines[@]} targets"
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
        (publish_target "$t_id" "$t_family" "$t_base" "$t_digest") \
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
echo "=== Publish summary ==="
if ((${#ok_list[@]} > 0)); then
    echo "Pass (${#ok_list[@]}): ${ok_list[*]}"
else
    echo "Pass (0): -"
fi
if ((${#fail_list[@]} > 0)); then
    echo "Fail (${#fail_list[@]}): ${fail_list[*]}" >&2
    exit 1
fi
