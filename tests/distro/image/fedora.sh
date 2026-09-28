# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Fedora family of tests/distro/image/packages.sh (sourced; defines
# family_runtime, family_build and family_krema). A separate file per family
# keeps the other families' image layers cached when this one changes.

fedora_install() {
    dnf -y --setopt=install_weak_deps=False install "$@"
}

# rubygem(logger): selenium-webdriver-at-spi-run requires logger, a bundled
# (no longer default) gem since Ruby 4.0 (Fedora 44+); Fedora 42/43 provide it
# through ruby-default-gems.
family_runtime() {
    fedora_install \
        kwin-wayland kglobalacceld \
        mesa-dri-drivers mesa-libEGL mesa-libgbm \
        dbus-daemon dbus-tools wayland-utils qt6-qttools \
        at-spi2-core at-spi2-atk python3-pyatspi \
        pipewire wireplumber pipewire-utils \
        breeze-icon-theme hicolor-icon-theme plasma-breeze kf6-qqc2-desktop-style xkeyboard-config \
        google-noto-sans-fonts dejavu-sans-fonts dejavu-sans-mono-fonts \
        kwrite kfind \
        ruby rubygems 'rubygem(logger)' \
        python3 python3-pip python3-flask python3-lxml python3-gobject python3-numpy \
        python3-pytest python3-pillow gtk3 gobject-introspection \
        procps-ng findutils gawk libcap psmisc
    dnf clean all
}

family_build() {
    fedora_install \
        extra-cmake-modules cmake ninja-build gcc-c++ git pkgconf-pkg-config \
        qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtwayland-devel \
        kf6-kwindowsystem-devel kf6-kcoreaddons-devel \
        kwayland-devel kpipewire-devel wayland-devel \
        plasma-wayland-protocols-devel libxkbcommon-devel
}

family_krema() {
    fedora_install /pkgs/*.rpm
    dnf clean all
}
