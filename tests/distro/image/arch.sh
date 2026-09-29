# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Arch Linux family of tests/distro/image/packages.sh (sourced; defines the
# family_* functions it calls). A separate file per family keeps the other
# families' image layers cached when this one changes. arch is rolling:
# packages.sh runs family_upgrade before every install phase.

arch_install() {
    pacman -S --noconfirm --needed "$@"
}

# Arch does not support partial upgrades: always sync and upgrade together.
# pacman's ALPM sandbox relies on seccomp filters and filesystem isolation
# that qemu-user cannot emulate (EINVAL); disable them when the first
# upgrade fails. Harmless in a throwaway container.
family_upgrade() {
    if ! pacman -Syu --noconfirm; then
        sed -i -e '/^\[options\]/a DisableSandboxSyscalls' \
            -e '/^\[options\]/a DisableSandboxFilesystem' /etc/pacman.conf
        pacman -Syu --noconfirm
    fi
}

family_runtime() {
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

# makepkg refuses to run as root: tests/distro/pkg/arch-build.sh builds as
# the unprivileged `builder`.
family_toolchain() {
    arch_install base-devel tar gzip ccache
    id -u builder >/dev/null 2>&1 || useradd -m -s /bin/bash builder
}

# depends + makedepends of the PKGBUILD. `pacman -T` is offline and applies
# the version constraints; makepkg (without -s) repeats that check. Missing
# ones are installed as root by name (sudo's setuid does not work under
# qemu-user, so `makepkg -s` is not an option).
family_builddeps() {
    missing=$(bash -c '
        source "$1"
        pacman -T "${depends[@]}" "${makedepends[@]}"
    ' _ "$PACKAGING/arch/PKGBUILD") || true
    if [ -z "$missing" ]; then
        echo "packages.sh: every depends/makedepends of the PKGBUILD is installed"
        return 0
    fi
    echo "packages.sh: missing PKGBUILD deps:" $missing
    # shellcheck disable=SC2086
    arch_install $(printf '%s\n' $missing | sed 's/[<>=].*//')
}

family_clean() {
    rm -rf /var/cache/pacman/pkg/*
}

# Download the package's declared runtime deps into the pacman cache (-Sw)
# while the package builds; the krema install then only runs the
# transaction. Nothing is installed here.
family_depfetch() {
    pacman -Sw --noconfirm "$@"
}

family_krema() {
    # Resolve dependencies against a current sync database (no partial
    # upgrades on Arch: upgrade along with it). pacman reuses prefetched
    # packages in /var/cache/pacman/pkg and downloads the rest.
    family_upgrade
    pacman -U --noconfirm --needed /pkgs/*.pkg.tar.zst
    # Shrink the committed diff: no cached packages.
    rm -rf /var/cache/pacman/pkg/*
}
