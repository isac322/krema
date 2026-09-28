# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# openSUSE family of tests/distro/image/packages.sh (sourced; defines
# family_runtime, family_build and family_krema). A separate file per family
# keeps the other families' image layers cached when this one changes.

suse_repos() {
    if [ "$TARGET_ID" = opensuse-slowroll ]; then
        # Same repo swap as tests/distro/pkg/rpm-build.sh: the base image is
        # opensuse/tumbleweed, packages must come from Slowroll.
        zypper --non-interactive modifyrepo --all --disable || true
        zypper --non-interactive addrepo --refresh \
            https://download.opensuse.org/slowroll/repo/oss/ slowroll-oss
        zypper --non-interactive addrepo --refresh \
            https://download.opensuse.org/update/slowroll/repo/oss/ slowroll-update
        zypper --non-interactive --gpg-auto-import-keys refresh
        zypper --non-interactive dup --allow-vendor-change
    else
        zypper --non-interactive --gpg-auto-import-keys refresh
    fi
}

suse_install() {
    zypper --non-interactive install --no-recommends "$@"
}

# python3-* names are capabilities of the primary python3XX-* flavor.
family_runtime() {
    suse_repos
    suse_install \
        kwin6 kglobalacceld6 \
        Mesa-dri Mesa-libEGL1 libgbm1 \
        dbus-1 dbus-1-tools dbus-1-daemon wayland-utils qt6-tools \
        at-spi2-core python3-atspi typelib-1_0-Atspi-2_0 \
        pipewire wireplumber pipewire-tools \
        kf6-breeze-icons hicolor-icon-theme breeze6-style kf6-qqc2-desktop-style xkeyboard-config \
        google-noto-sans-fonts dejavu-fonts \
        kwrite kfind \
        ruby \
        python3 python3-pip python3-Flask python3-lxml python3-gobject python3-numpy \
        python3-pytest python3-Pillow typelib-1_0-Gtk-3_0 \
        procps findutils gawk coreutils libcap-progs psmisc
    zypper --non-interactive clean --all
}

family_build() {
    # --force-resolution: the -devel packages need GNU diffutils, which the
    # base image's busybox-diffutils blocks unless zypper may replace it.
    suse_install --force-resolution \
        extra-cmake-modules cmake ninja gcc-c++ git pkgconf \
        qt6-base-devel qt6-base-private-devel qt6-wayland-devel qt6-wayland-private-devel \
        kf6-kwindowsystem-devel kf6-kcoreaddons-devel \
        kwayland6-devel kpipewire6-devel wayland-devel \
        plasma-wayland-protocols libxkbcommon-devel
}

family_krema() {
    zypper --non-interactive --gpg-auto-import-keys refresh
    suse_install --allow-unsigned-rpm /pkgs/*.rpm
    zypper --non-interactive clean --all
}
