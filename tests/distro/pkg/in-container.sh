#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Container entrypoint for tests/distro/build-package.sh, in the target's
# builder image (tests/distro/image/Dockerfile --target builder). Never run
# on the host.
#
# Mounts provided by the host script:
#   /stage           read-only, contains krema-<version>.tar.gz
#   /pkg/packaging   read-only copy of the repo's packaging/ directory
#   /pkg/bin         this helper directory
#   /out             writable output directory for built packages
#   /ccache          ccache directory (host KREMA_CCACHE_DIR)
#
# Environment: TARGET_ID, FAMILY, KREMA_VERSION; HOST_UID/HOST_GID (owner
# given back to /out and /ccache on exit); CCACHE_MAXSIZE (default 500M).
#
# Steps: rolling upgrade (packages.sh upgrade, a no-op on stable targets),
# missing build deps (packages.sh builddeps), then the family's package build
# with the compilers behind ccache.

set -euo pipefail

: "${TARGET_ID:?}" "${FAMILY:?}" "${KREMA_VERSION:?}"

bin_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
packages=/usr/local/lib/krema-distro/packages.sh
[[ -f "$packages" ]] || { echo "error: $packages missing: not a krema builder image" >&2; exit 66; }

case "$FAMILY" in
    fedora|suse) builder=rpm-build.sh ;;
    debian)      builder=deb-build.sh ;;
    arch)        builder=arch-build.sh ;;
    *) echo "error: unknown FAMILY '$FAMILY'" >&2; exit 64 ;;
esac

tarball="/stage/krema-${KREMA_VERSION}.tar.gz"
[[ -f "$tarball" ]] || { echo "error: $tarball missing" >&2; exit 66; }

umask 022
mkdir -p /work /out

give_back() {
    [[ -n "${HOST_UID:-}" ]] || return 0
    chown -R "$HOST_UID:${HOST_GID:-$HOST_UID}" /out
    [[ ! -d /ccache ]] || chown -R "$HOST_UID:${HOST_GID:-$HOST_UID}" /ccache
}
trap give_back EXIT

# phase <name> <command...>: runs the command and prints its wall time.
phase() {
    local name="$1" t0=$SECONDS
    shift
    "$@"
    echo "[distro] $name: $((SECONDS - t0))s"
}

phase builder-upgrade sh "$packages" upgrade
phase build-deps sh "$packages" builddeps

# CMake (>= 3.17) initialises CMAKE_<LANG>_COMPILER_LAUNCHER from these
# variables, and rpmbuild, dpkg-buildpackage/debhelper and makepkg pass the
# environment through to it. /work is the only build root, so CCACHE_BASEDIR
# makes the cache keys path-independent; CCACHE_NOHASHDIR keeps them
# independent of the versioned source directory too (it only affects the
# DW_AT_comp_dir of a cache hit's debug info).
if [[ -d /ccache ]] && command -v ccache >/dev/null; then
    export CCACHE_DIR=/ccache CCACHE_BASEDIR=/work CCACHE_NOHASHDIR=1
    export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-500M}"
    export CMAKE_C_COMPILER_LAUNCHER=ccache CMAKE_CXX_COMPILER_LAUNCHER=ccache
    ccache --zero-stats >/dev/null
else
    echo "in-container: building without ccache (no /ccache mount or no ccache binary)"
fi

phase package-build bash "$bin_dir/$builder"

if [[ -n "${CCACHE_DIR:-}" ]]; then
    ccache --show-stats
fi
