# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Debian/Ubuntu family of tests/distro/image/packages.sh (sourced; defines the
# family_* functions it calls). A separate file per family keeps the other
# families' image layers cached when this one changes. No target of this
# family is rolling, so there is no family_upgrade.

export DEBIAN_FRONTEND=noninteractive

debian_install() {
    apt-get install -y --no-install-recommends "$@"
}

# Packages that only some releases have (split or renamed across Debian 13 and
# Ubuntu 25.04..26.04): kept only when the archive has an installable version
# (a package another one replaced is known to apt but has no candidate).
debian_available() {
    for p do
        if apt-cache policy "$p" 2>/dev/null | grep -q 'Candidate: [^(]'; then
            echo "$p"
        fi
    done
}

family_runtime() {
    apt-get update
    # mesa-vulkan-drivers (lavapipe): without a Vulkan ICD, Mesa 25.0 (Ubuntu
    # 25.04) can deadlock in its zink fallback while creating a GL context.
    # qt6-svg-plugins: Breeze icons are SVG. A Plasma session has the Qt SVG
    # image/icon-engine plugins (Fedora and openSUSE pull them in with
    # qt6-qtsvg); here nothing does, and krema and KWin would draw
    # placeholders instead of app icons.
    # shellcheck disable=SC2046
    debian_install \
        kwin-wayland $(debian_available kglobalacceld) \
        libgl1-mesa-dri libegl1 libgbm1 mesa-vulkan-drivers \
        dbus-daemon dbus-bin dbus-session-bus-common wayland-utils \
        at-spi2-core libatk-adaptor python3-pyatspi gir1.2-atspi-2.0 \
        pipewire pipewire-bin wireplumber \
        breeze-icon-theme hicolor-icon-theme breeze qml6-module-org-kde-desktop qt6-svg-plugins xkb-data \
        fonts-noto-core fonts-dejavu-core \
        kwrite kfind \
        ruby \
        python3 python3-pip python3-flask python3-lxml python3-gi python3-numpy \
        python3-pytest python3-pil gir1.2-gtk-3.0 \
        procps findutils gawk libcap2-bin psmisc ca-certificates
    rm -rf /var/lib/apt/lists/*
}

family_build() {
    apt-get update
    # qtwaylandscanner: qt6-wayland-dev-tools until Qt 6.9, qt6-base-dev-tools
    # since Qt 6.10 moved it into qtbase (Ubuntu 26.04).
    # shellcheck disable=SC2046
    debian_install \
        extra-cmake-modules cmake ninja-build g++ git pkg-config \
        qt6-base-dev qt6-base-private-dev qt6-base-dev-tools qt6-wayland-dev qt6-wayland-private-dev \
        $(debian_available qt6-wayland-dev-tools) \
        libkf6windowsystem-dev libkf6coreaddons-dev kwayland-dev libkpipewire-dev \
        libwayland-dev plasma-wayland-protocols libxkbcommon-dev
}

family_toolchain() {
    apt-get update
    debian_install build-essential dpkg-dev fakeroot devscripts equivs tar xz-utils ccache
}

# dpkg-checkbuilddeps is offline and applies the version constraints;
# dpkg-buildpackage runs the same check before building. Missing deps are
# installed through a generated metapackage (handles virtual packages such as
# debhelper-compat and arch-qualified deps).
family_builddeps() {
    control="$PACKAGING/obs/debian.control"
    if dpkg-checkbuilddeps "$control"; then
        echo "packages.sh: every Build-Depends of debian.control is installed"
        return 0
    fi
    apt-get update
    tmp=$(mktemp -d)
    (cd "$tmp" && mk-build-deps --install --remove \
        --tool 'apt-get -y --no-install-recommends' "$control")
    rm -rf "$tmp"
}

family_clean() {
    rm -rf /var/lib/apt/lists/*
}

# Download the package's declared runtime deps into the apt archive cache
# while the package builds; the krema install then only runs the
# transaction. Nothing is installed here. apt-get update refreshes the
# lists the runtime image cleaned.
family_depfetch() {
    apt-get update
    debian_install --download-only "$@"
}

family_krema() {
    if ! debian_install /pkgs/*.deb; then
        # Stale lists (or a depfetch from an older snapshot) can reference
        # versions the archive no longer has: refresh and retry once.
        apt-get update
        debian_install /pkgs/*.deb
    fi
    # Shrink the committed diff: no .deb files or lists.
    apt-get clean
    rm -rf /var/lib/apt/lists/*
}
