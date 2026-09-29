#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Arch build using packaging/arch/PKGBUILD inside the arch builder image, with
# the source redirected from the GitHub release tarball to the local tarball.
# Invoked by in-container.sh after the upgrade and the depends/makedepends
# install (as root: sudo's setuid does not work under qemu-user emulation, so
# `makepkg -s` is not an option); makepkg re-checks them.

set -euo pipefail

# makepkg refuses to run as root; build as the image's unprivileged user.
id -u builder >/dev/null 2>&1 || useradd -m -s /bin/bash builder
chown -R builder:builder /work
[[ -z "${CCACHE_DIR:-}" ]] || chown -R builder:builder "$CCACHE_DIR"

# su without --login keeps the environment (CCACHE_*, CMAKE_*_LAUNCHER).
su builder -c "
set -euo pipefail
cd /work
cp /stage/krema-${KREMA_VERSION}.tar.gz .

# Redirect source= to the local tarball, keeping the same extracted
# directory name (\$pkgname-\$pkgver).
sed 's|^source=.*|source=(\"krema-${KREMA_VERSION}.tar.gz\")|' \
    /pkg/packaging/arch/PKGBUILD > PKGBUILD

# Regenerate the checksum for the local tarball.
sed -i '/^sha256sums=/d' PKGBUILD
newsums=\$(makepkg -g)
awk -v repl=\"\$newsums\" '
    /^source=/ { print; print repl; next }
    { print }
' PKGBUILD > PKGBUILD.new && mv PKGBUILD.new PKGBUILD

makepkg --noconfirm
"

shopt -s nullglob
# Ship the binary package only; krema-debug-* is the automatic debug split
# (kept on: it is what adds .gnu_debuglink to the shipped binary).
artifacts=(/work/krema-[0-9]*.pkg.tar.zst)
if (( ${#artifacts[@]} == 0 )); then
    echo "error: makepkg finished but produced no package" >&2
    exit 70
fi
cp "${artifacts[@]}" /out/
ls -l /out
