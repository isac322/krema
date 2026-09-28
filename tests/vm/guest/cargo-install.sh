#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
# Runs inside the VM as root via krema-cargo-install.service, before sddm.
# Mounts the KREMACARGO ISO, installs the Krema package(s) with the distro
# package manager (so dependency resolution is exercised), and leaves the ISO
# mounted at /run/krema-cargo for the guest verifier.
#
# On the provisioning boot the ISO is absent: the unit must exit 0 quickly so
# it never blocks sddm there.

set -uo pipefail

log() { echo "[krema-cargo-install] $*"; }
die() { echo "[krema-cargo-install] ERROR: $*" >&2; exit "${2:-1}"; }

ISO_LABEL=KREMACARGO
MNT=/run/krema-cargo
PKGGLOB="${KREMA_PACKAGE_GLOB:-}"

# The cargo ISO may still be probing when we start; wait briefly.
dev=""
for _ in $(seq 1 30); do
    for c in "/dev/disk/by-label/$ISO_LABEL" "/dev/disk/by-label/${ISO_LABEL,,}"; do
        if [[ -e $c ]]; then dev="$(readlink -f "$c")"; break; fi
    done
    [[ -n $dev ]] && break
    # Fallback: find the iso9660 device blkid knows about.
    dev="$(blkid -t "LABEL=$ISO_LABEL" -o device 2>/dev/null | head -n1 || true)"
    [[ -n $dev ]] && break
    sleep 1
done
if [[ -z $dev ]]; then
    log "no $ISO_LABEL device found — provisioning boot, nothing to install"
    exit 0
fi

mkdir -p "$MNT"
if ! mountpoint -q "$MNT"; then
    mount -o ro "$dev" "$MNT" || die "cannot mount $dev on $MNT"
fi
log "cargo ISO mounted at $MNT"

shopt -s nullglob
packages=("$MNT/packages/"*.rpm "$MNT/packages/"*.deb "$MNT/packages/"*.pkg.tar.zst)
if [[ -n $PKGGLOB ]]; then
    packages=("$MNT/packages/"$PKGGLOB)
fi
shopt -u nullglob
((${#packages[@]})) || die "no packages under $MNT/packages"

# Record what the ISO carries for the verifier's version check.
for p in "${packages[@]}"; do basename "$p"; done > "$MNT/../krema-cargo-manifest" 2>/dev/null || true

# Install through the distro package manager so dependencies resolve from the
# distribution repositories exactly like a user install would. Retry in case
# the network is still coming up (this unit races multi-user boot).
pm_install() {
    if command -v dnf >/dev/null 2>&1; then
        dnf -y install "$@"
    elif command -v zypper >/dev/null 2>&1; then
        zypper --non-interactive --no-gpg-checks install "$@"
    elif command -v apt-get >/dev/null 2>&1; then
        apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y "$@"
    elif command -v pacman >/dev/null 2>&1; then
        # -U installs local package files; -Sy first so dependencies resolve
        # against current repo databases.
        pacman -Sy --noconfirm && pacman -U --noconfirm --needed "$@"
    else
        return 127
    fi
}
installed=0
for attempt in 1 2 3; do
    log "install attempt $attempt: ${packages[*]##*/}"
    if pm_install "${packages[@]}"; then installed=1; break; fi
    sleep 10
done
((installed)) || die "package install failed after 3 attempts"
log "installed: ${packages[*]##*/}"

# Fallback autostart for packages that ship no /etc/xdg/autostart entry.
if ! compgen -G "/etc/xdg/autostart/*krema*" >/dev/null \
    && ! compgen -G "/usr/etc/xdg/autostart/*krema*" >/dev/null; then
    app="$(compgen -G "/usr/share/applications/*krema*.desktop" | head -n1 || true)"
    if [[ -n $app ]]; then
        log "no system autostart entry; adding user autostart for qa"
        install -d -m 0755 -o qa -g qa /home/qa/.config/autostart
        install -m 0644 -o qa -g qa "$app" /home/qa/.config/autostart/
    else
        log "WARNING: no krema autostart or applications desktop file found"
    fi
fi

touch /var/lib/krema-cargo-installed
sync
