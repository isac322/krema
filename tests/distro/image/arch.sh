# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Arch Linux family of tests/distro/image/packages.sh (sourced; defines
# family_runtime, family_build and family_krema). A separate file per family
# keeps the other families' image layers cached when this one changes.

arch_install() {
    pacman -S --noconfirm --needed "$@"
}

family_runtime() {
    # Arch does not support partial upgrades: sync and upgrade first.
    pacman -Syu --noconfirm
    arch_install \
        kwin kglobalacceld \
        mesa \
        dbus wayland-utils qt6-tools \
        at-spi2-core python-atspi \
        pipewire wireplumber \
        breeze-icons hicolor-icon-theme breeze qqc2-desktop-style xkeyboard-config \
        noto-fonts ttf-dejavu \
        kwrite kfind \
        ruby \
        python python-pip python-flask python-lxml python-gobject python-numpy \
        python-pytest python-pillow gtk3 \
        procps-ng findutils gawk libcap psmisc
    rm -rf /var/cache/pacman/pkg/*
}

family_build() {
    arch_install \
        extra-cmake-modules cmake ninja gcc git pkgconf \
        qt6-base qt6-wayland kwindowsystem kcoreaddons \
        kwayland kpipewire wayland plasma-wayland-protocols libxkbcommon
}

family_krema() {
    # Resolve dependencies against a current sync database (no partial
    # upgrades on Arch: upgrade along with it).
    pacman -Syu --noconfirm
    pacman -U --noconfirm --needed /pkgs/*.pkg.tar.zst
    rm -rf /var/cache/pacman/pkg/*
}
