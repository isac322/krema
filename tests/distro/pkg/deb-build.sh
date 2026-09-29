#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Debian family build (debian, ubuntu) using packaging/obs/debian.* inside the
# target's builder image. Invoked by in-container.sh after the build deps are
# installed; dpkg-buildpackage re-checks Build-Depends with their versions.

set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

# Unpack the source tarball and drop the OBS debian.* files in as debian/.
src="/work/krema-${KREMA_VERSION}"
tar -xzf "/stage/krema-${KREMA_VERSION}.tar.gz" -C /work
mkdir -p "$src/debian"
install -m644 /pkg/packaging/obs/debian.control "$src/debian/control"
install -m644 /pkg/packaging/obs/debian.changelog "$src/debian/changelog"
install -m644 /pkg/packaging/obs/debian.copyright "$src/debian/copyright"
install -m755 /pkg/packaging/obs/debian.rules "$src/debian/rules"

cd "$src"
# The automatic dbgsym package stays on: dh_strip only adds the
# .gnu_debuglink section to the shipped binary when it saves the debug
# symbols, so noautodbgsym would change the tested binary.
dpkg-buildpackage -b -us -uc

# Artifacts land in the parent of the source tree (/work). Ship the binary
# .deb only; the dbgsym package (krema-dbgsym_*.ddeb/.deb) is never installed.
shopt -s nullglob
artifacts=(/work/krema_*.deb)
if (( ${#artifacts[@]} == 0 )); then
    echo "error: dpkg-buildpackage finished but produced no .deb" >&2
    exit 70
fi
cp "${artifacts[@]}" /out/
ls -l /out
