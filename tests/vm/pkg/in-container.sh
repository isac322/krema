#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Container entrypoint for tests/vm/build-package.sh. Never run on the host.
#
# Mounts provided by the host script:
#   /stage           read-only, contains krema-<version>.tar.gz
#   /pkg/packaging   read-only copy of the repo's packaging/ directory
#   /pkg/bin         this helper directory
#   /out             writable output directory for built packages
#
# Environment: TARGET_ID, FAMILY, KREMA_VERSION

set -euo pipefail

: "${TARGET_ID:?}" "${FAMILY:?}" "${KREMA_VERSION:?}"

bin_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

case "$FAMILY" in
    fedora|suse) builder=rpm-build.sh ;;
    debian)      builder=deb-build.sh ;;
    arch)        builder=arch-build.sh ;;
    *) echo "error: unknown FAMILY '$FAMILY'" >&2; exit 64 ;;
esac

tarball="/stage/krema-${KREMA_VERSION}.tar.gz"
[[ -f "$tarball" ]] || { echo "error: $tarball missing" >&2; exit 66; }

mkdir -p /work /out
exec bash "$bin_dir/$builder"
