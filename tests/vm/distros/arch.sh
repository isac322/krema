# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
# Arch Linux distro plug-in for tests/vm/run-vm-qa.sh.
# Covers target: arch (x86_64 only — Arch publishes no aarch64 cloud image).
#
# Contract with run-vm-qa.sh (sourced, TARGET_ID and FAMILY already set):
#   DISTRO_PKG_GLOB        package file glob for the cargo ISO
#   distro_image()         prints "<url> <sha256|->" of the cloud image
#   distro_provision()     prints bash that runs inside krema-provision.sh
#                          (root, during first boot) installing the desktop

DISTRO_PKG_GLOB='*.pkg.tar.zst'

ARCH_IMAGE_BASE="${KREMA_VM_ARCH_IMAGE_BASE:-https://geo.mirror.pkgbuild.com/images/latest}"

# The official arch-boxes cloud image ships a .SHA256 sidecar next to the
# 'latest' alias, so the checksum (and thus the golden-image fingerprint)
# follows every upstream rebuild.
distro_image() {
    local arch fname sha
    arch="$(krema_arch)"
    [[ $arch == x86_64 ]] \
        || die "Arch Linux publishes x86_64 cloud images only (host: $arch)"
    fname="Arch-Linux-x86_64-cloudimg.qcow2"
    sha="$(fetch "$ARCH_IMAGE_BASE/$fname.SHA256" 2>/dev/null \
        | awk -v f="$fname" '$2 == f { print $1; exit }')"
    [[ $sha =~ ^[0-9a-f]{64}$ ]] || sha='-'
    echo "$ARCH_IMAGE_BASE/$fname $sha"
}

# Minimal Plasma 6 Wayland + SDDM (Wayland greeter on kwin) + QA tooling,
# using Arch package names. Runs as root during the first (provisioning) boot.
distro_provision() {
    cat <<'EOS'
log "configuring pacman"
# arch-boxes initialises the keyring in pacman-init.service; make sure it ran.
systemctl start pacman-init.service 2>/dev/null || true
cat > /etc/pacman.d/mirrorlist <<'EOF'
Server = https://geo.mirror.pkgbuild.com/$repo/os/$arch
Server = https://fastly.mirror.pkgbuild.com/$repo/os/$arch
EOF
sed -i -E 's/^#?ParallelDownloads.*/ParallelDownloads = 8/' /etc/pacman.conf

log "installing Plasma desktop and QA tooling (pacman)"
# Refresh the keyring first so packages signed by newly added keys verify,
# then a full upgrade (Arch does not support partial upgrades).
pacman -Sy --noconfirm --needed archlinux-keyring
pacman -Su --noconfirm
pacman -S --noconfirm --needed \
    plasma-desktop \
    plasma-workspace \
    kwin \
    sddm \
    breeze \
    pipewire \
    pipewire-pulse \
    wireplumber \
    xdg-desktop-portal-kde \
    at-spi2-core \
    python-atspi \
    python-gobject \
    konsole \
    kwrite \
    noto-fonts \
    qemu-guest-agent
pacman -Scc --noconfirm || true

# Arch's sddm defaults to an X11 greeter and no Xorg is installed; run the
# greeter on kwin_wayland like Plasma's upstream-recommended configuration.
mkdir -p /etc/sddm.conf.d
cat > /etc/sddm.conf.d/10-wayland.conf <<'EOF'
[General]
DisplayServer=wayland
GreeterEnvironment=QT_WAYLAND_SHELL_INTEGRATION=layer-shell

[Wayland]
CompositorCommand=kwin_wayland --drm --no-lockscreen --no-global-shortcuts --locale1
EOF

systemctl enable sddm.service
systemctl set-default graphical.target
EOS
}
