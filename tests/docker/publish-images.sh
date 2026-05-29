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
target_filter="${1:-all}"

usage() {
    cat <<'EOF'
Usage: tests/docker/publish-images.sh [target-id|all]

Builds runtime smoke images with `docker buildx` from the digest-pinned
base recorded in targets.tsv and pushes them to GHCR as OCI manifests.
Slowroll is published linux/amd64-only; every other target is
linux/amd64,linux/arm64.

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
        exit 65
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

if ((dry_run == 0)); then
    ensure_builder
fi

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
    publish_target "$target_id" "$family" "$base_image" "$base_digest"
done <"$targets_file"

if ((matched == 0)); then
    echo "No target matched: $target_filter" >&2
    exit 64
fi
