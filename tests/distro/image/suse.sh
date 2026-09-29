# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# openSUSE family of tests/distro/image/packages.sh (sourced; defines the
# family_* functions it calls). A separate file per family keeps the other
# families' image layers cached when this one changes.

# Refresh the repositories. opensuse-slowroll's base image is
# opensuse/tumbleweed: its first call swaps the repositories to Slowroll's
# (family_upgrade then dups onto them).
suse_repos() {
    if [ "$TARGET_ID" = opensuse-slowroll ] \
        && ! zypper --non-interactive repos | grep -qw slowroll-oss; then
        zypper --non-interactive modifyrepo --all --disable || true
        zypper --non-interactive addrepo --refresh \
            https://download.opensuse.org/slowroll/repo/oss/ slowroll-oss
        zypper --non-interactive addrepo --refresh \
            https://download.opensuse.org/update/slowroll/repo/oss/ slowroll-update
    fi
    zypper --non-interactive --gpg-auto-import-keys refresh
}

suse_install() {
    zypper --non-interactive install --no-recommends "$@"
}

family_upgrade() {
    suse_repos
    zypper --non-interactive dup --no-recommends --allow-vendor-change
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

family_toolchain() {
    suse_repos
    suse_install rpm-build tar gzip findutils grep sed gawk
    # ccache only speeds the package build up; a distro without it still
    # builds the same package.
    suse_install ccache || echo "packages.sh: ccache not installable on $TARGET_ID, building without it"
}

# zypper has no builddep: install the spec's missing BuildRequires as
# capabilities. `rpmspec` evaluates %if suse_version, so the correct
# ninja/ninja-build alternative is picked.
family_builddeps() {
    missing=$(rpm_missing_buildreqs)
    if [ -z "$missing" ]; then
        echo "packages.sh: every BuildRequires of krema.spec is installed"
        return 0
    fi
    echo "packages.sh: missing BuildRequires:" $missing
    suse_repos
    # shellcheck disable=SC2086
    suse_install $missing
}

family_clean() {
    zypper --non-interactive clean --all
}

# Download the package's declared runtime deps into the zypper package cache
# (keeppackages on the repos, --download-only) while the package builds; the
# krema install then only runs the transaction. Nothing is installed here.
family_depfetch() {
    zypper --non-interactive modifyrepo -k --all
    suse_repos
    zypper --non-interactive install --no-recommends --download-only "$@"
}

family_krema() {
    if ! suse_install --allow-unsigned-rpm /pkgs/*.rpm; then
        # A stale metadata snapshot can reference packages the repos no
        # longer publish: refresh and retry once.
        zypper --non-interactive --gpg-auto-import-keys refresh --force
        suse_install --allow-unsigned-rpm /pkgs/*.rpm
    fi
    # Shrink the committed diff: no cached packages or metadata.
    zypper --non-interactive clean --all
}
