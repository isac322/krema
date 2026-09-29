# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Fedora family of tests/distro/image/packages.sh (sourced; defines the
# family_* functions it calls). A separate file per family keeps the other
# families' image layers cached when this one changes.

fedora_install() {
    dnf -y --setopt=install_weak_deps=False install "$@"
}

family_upgrade() {
    dnf -y --setopt=install_weak_deps=False --refresh distro-sync
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

family_toolchain() {
    fedora_install 'dnf-command(builddep)' rpm-build tar gzip findutils ccache \
        || fedora_install dnf-plugins-core rpm-build tar gzip findutils ccache
}

family_builddeps() {
    missing=$(rpm_missing_buildreqs)
    if [ -z "$missing" ]; then
        echo "packages.sh: every BuildRequires of krema.spec is installed"
        return 0
    fi
    echo "packages.sh: missing BuildRequires:" $missing
    dnf -y builddep "$PACKAGING/obs/krema.spec"
}

family_clean() {
    dnf clean all
}

# Download the package's declared runtime deps into the dnf package cache
# (--downloadonly + keepcache) while the package builds; the krema install
# then only runs the transaction. Nothing is installed here.
family_depfetch() {
    dnf -y --setopt=install_weak_deps=False --setopt=keepcache=True \
        install --downloadonly "$@"
}

family_krema() {
    if ! fedora_install /pkgs/*.rpm; then
        # The prefetched cache may hold metadata whose packages no longer
        # exist (newer repo snapshot): refresh and retry once.
        dnf -y clean all
        fedora_install /pkgs/*.rpm
    fi
    # Shrink the committed diff: no cached packages or metadata.
    dnf clean all
}
