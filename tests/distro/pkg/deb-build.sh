#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Debian family build (debian, ubuntu) using packaging/obs/debian.* inside the
# target's base container. Invoked by in-container.sh.

set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y --no-install-recommends \
    build-essential fakeroot devscripts equivs tar xz-utils

# Unpack the source tarball and drop the OBS debian.* files in as debian/.
src="/work/krema-${KREMA_VERSION}"
tar -xzf "/stage/krema-${KREMA_VERSION}.tar.gz" -C /work
mkdir -p "$src/debian"
install -m644 /pkg/packaging/obs/debian.control "$src/debian/control"
install -m644 /pkg/packaging/obs/debian.changelog "$src/debian/changelog"
install -m644 /pkg/packaging/obs/debian.copyright "$src/debian/copyright"
install -m755 /pkg/packaging/obs/debian.rules "$src/debian/rules"

# Install Build-Depends via a generated metapackage (handles virtual packages
# such as debhelper-compat and arch-qualified deps).
mk-build-deps --install --remove \
    --tool 'apt-get -y --no-install-recommends' "$src/debian/control"

cd "$src"
dpkg-buildpackage -b -us -uc

# Artifacts land in the parent of the source tree (/work). Ship the binary
# .deb plus the .ddeb dbgsym package where the distro produces one.
shopt -s nullglob
artifacts=(/work/krema_*.deb /work/krema_*.ddeb)
if (( ${#artifacts[@]} == 0 )); then
    echo "error: dpkg-buildpackage finished but produced no .deb" >&2
    exit 70
fi
cp "${artifacts[@]}" /out/
ls -l /out
