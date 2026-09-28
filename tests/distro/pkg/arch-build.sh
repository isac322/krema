#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Arch build using packaging/arch/PKGBUILD inside an archlinux container, with
# the source redirected from the GitHub release tarball to the local tarball.
# Invoked by in-container.sh.

set -euo pipefail

# pacman's ALPM sandbox relies on seccomp filters and filesystem isolation
# that qemu-user cannot emulate (EINVAL); fall back to disabling them when the
# first sync fails. Harmless in a throwaway build container.
if ! pacman -Sy --noconfirm; then
    sed -i -e '/^\[options\]/a DisableSandboxSyscalls' \
           -e '/^\[options\]/a DisableSandboxFilesystem' /etc/pacman.conf
    pacman -Sy --noconfirm
fi
pacman -S --noconfirm --needed base-devel tar gzip

# Install every depends/makedepends entry of the PKGBUILD as root, then run
# makepkg without -s. sudo's setuid does not work under qemu-user emulation,
# so a root pre-install is both simpler and more reliable than `makepkg -s`.
# Dep names come straight from the PKGBUILD arrays; '>=' constraints are
# stripped (current rolling packages satisfy them).
mapfile -t deps < <(
    bash -c '
        source /pkg/packaging/arch/PKGBUILD
        for d in "${depends[@]}" "${makedepends[@]}"; do
            printf "%s\n" "${d%%[<>=]*}"
        done
    ' | sort -u
)
pacman -S --noconfirm --needed "${deps[@]}"

# makepkg refuses to run as root; build as an unprivileged user.
useradd -m -s /bin/bash builder
chown -R builder:builder /work

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
# Ship the binary package only; krema-debug-* is the automatic debug split.
artifacts=(/work/krema-[0-9]*.pkg.tar.zst)
if (( ${#artifacts[@]} == 0 )); then
    echo "error: makepkg finished but produced no package" >&2
    exit 70
fi
cp "${artifacts[@]}" /out/
ls -l /out
