# syntax=docker/dockerfile:1
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Self-contained UI test image: builds krema from source and runs it against an
# in-container `kwin_wayland --virtual` compositor. Unlike tests/docker/, this
# image contains the compositor, so nothing on the CI host is required beyond
# an unprivileged container runtime.

ARG BASE_IMAGE=fedora:43
FROM ${BASE_IMAGE}

# kwrite/kfind/okular/gwenview are installed for their .desktop files and
# icons, not to run them. Their icon names all exist in breeze; kcalc and
# kcharselect were rejected because their Icon= keys (accessories-calculator,
# accessories-character-map) resolve only to a 16 px Adwaita fallback here. Without a resolvable launcher the dock falls back to a
# generic placeholder for every item, which hides real icon regressions and
# makes every captured frame look broken.

LABEL org.opencontainers.image.title="Krema UI frame-test image" \
      org.opencontainers.image.description="kwin_wayland --virtual plus krema build deps for deterministic frame-stepped UI tests" \
      org.opencontainers.image.source="https://github.com/isac322/krema"

ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

SHELL ["/bin/sh", "-euxc"]

RUN dnf -y --setopt=install_weak_deps=False install \
        kwin-wayland \
        mesa-dri-drivers mesa-libEGL mesa-libgbm \
        dbus-daemon dbus-tools wayland-utils \
        breeze-icon-theme hicolor-icon-theme \
        kwrite kfind okular gwenview \
        extra-cmake-modules cmake ninja-build gcc-c++ pkgconf-pkg-config \
        qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtwayland-devel \
        qt6-qtshadertools-devel qt6-qtquickcontrols2 \
        kf6-kwindowsystem-devel kf6-kconfig-devel kf6-kcoreaddons-devel \
        kf6-kdbusaddons-devel kf6-ki18n-devel kf6-kglobalaccel-devel \
        kf6-kcolorscheme-devel kf6-kiconthemes-devel kf6-kcrash-devel \
        kf6-kxmlgui-devel kf6-kservice-devel kf6-kitemmodels-devel \
        kf6-kirigami-devel kf6-kirigami-addons kf6-kirigami-addons-devel \
        plasma-workspace-devel kpipewire-devel \
        layer-shell-qt layer-shell-qt-devel wayland-devel \
        python3 python3-pillow diffutils procps-ng findutils libcap \
        ffmpeg-free google-droid-sans-fonts \
    && dnf clean all

# kwin_wayland ships with cap_sys_nice=ep. An unprivileged container's bounding
# capability set has no CAP_SYS_NICE, so execve() fails outright with EPERM
# ("Operation not permitted") before main() runs. Dropping the file capability
# is what makes the compositor startable without --cap-add or --privileged.
RUN setcap -r /usr/sbin/kwin_wayland

# A launcher with a deliberately long name. QA-ITEM-011 checks that the hover
# tooltip stays on one line and is not truncated; every real app installed here
# has a short name, which would make that check pass for the wrong reason.
RUN printf '%s\n' \
        '[Desktop Entry]' \
        'Type=Application' \
        'Name=Krema Tooltip Layout Fixture Application' \
        'Comment=Long name fixture for tooltip layout tests' \
        'Exec=/bin/true' \
        'Icon=applications-graphics' \
        'Categories=Utility;' \
        > /usr/share/applications/krema-longname-fixture.desktop

WORKDIR /work
