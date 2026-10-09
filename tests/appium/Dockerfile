# syntax=docker/dockerfile:1
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Krema E2E image: krema build deps + kwin_wayland --virtual + AT-SPI +
# PipeWire + KDE's selenium-webdriver-at-spi (built from source) + a small
# deterministic test-window fixture. Build context: tests/appium/.
#
# Works unprivileged (plain `docker run`, no --privileged, no /dev/dri, no KVM)
# on linux/amd64 and linux/arm64.
#
# `ctest-image` is a side stage of `base` with only ccache added: the
# "Build & tests" job in .github/workflows/e2e.yml builds `--target
# ctest-image` because it runs the CTest suites, not the AT-SPI suite.

ARG BASE_IMAGE=fedora:43
FROM ${BASE_IMAGE} AS base

ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked \
    dnf -y --setopt=install_weak_deps=False --setopt=keepcache=True install \
        kwin-wayland kglobalacceld \
        mesa-dri-drivers mesa-libEGL mesa-libgbm \
        dbus-daemon dbus-tools wayland-utils qt6-qttools \
        at-spi2-core at-spi2-atk python3-pyatspi \
        pipewire wireplumber pipewire-utils \
        breeze-icon-theme hicolor-icon-theme plasma-breeze kf6-qqc2-desktop-style xkeyboard-config \
        google-noto-sans-fonts dejavu-sans-fonts dejavu-sans-mono-fonts \
        kwrite kfind konsole dolphin \
        extra-cmake-modules cmake ninja-build gcc-c++ pkgconf-pkg-config git \
        qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel qt6-qtwayland-devel \
        qt6-qtshadertools-devel qt6-qtquickcontrols2 \
        qt6-qtquick3d \
        kf6-kwindowsystem-devel kf6-kconfig-devel kf6-kcoreaddons-devel \
        kf6-kdbusaddons-devel kf6-ki18n-devel kf6-kglobalaccel-devel \
        kf6-kcolorscheme-devel kf6-kiconthemes-devel kf6-kcrash-devel \
        kf6-kxmlgui-devel kf6-kservice-devel kf6-kitemmodels-devel \
        kf6-kirigami-devel kf6-kirigami-addons kf6-kirigami-addons-devel \
        plasma-workspace-devel kpipewire-devel \
        layer-shell-qt layer-shell-qt-devel wayland-devel \
        kwayland-devel plasma-wayland-protocols-devel libxkbcommon-devel \
        catch-devel \
        ruby rubygems \
        python3 python3-pip python3-flask python3-lxml python3-gobject python3-numpy \
        python3-pytest python3-pillow gtk3 gobject-introspection \
        procps-ng findutils libcap rsync psmisc

# kwin_wayland ships with cap_sys_nice=ep. An unprivileged container's bounding
# capability set has no CAP_SYS_NICE, so execve() fails with EPERM before
# main() runs. Dropping the file capability makes it startable without
# --cap-add or --privileged.
RUN setcap -r /usr/sbin/kwin_wayland

# Client side of the WebDriver protocol. Versions pinned to the upstream
# requirements.txt of the pinned selenium-webdriver-at-spi commit. Flask,
# lxml, PyGObject and numpy come from Fedora above.
RUN pip3 install --no-cache-dir --break-system-packages \
        Appium-Python-Client==4.5.1 selenium==4.29.0 pytest-timeout==2.3.1

FROM base AS swas-build
# KDE selenium-webdriver-at-spi. Not packaged in Fedora 43 -> build a pinned
# commit (master 2026-06-29, "Fix focus action name with Qt 6.12").
ARG SWAS_COMMIT=d45a21e8f1b3591dc921f0be85f1ecd834cbe413
COPY tools/inputsynth-fixes.patch tools/run-output-count.patch tools/run-permission-checks.patch tools/session-attach-probe-first.patch tools/pipelined-tree.patch /src/
RUN git init /src/swas && cd /src/swas \
    && git remote add origin https://invent.kde.org/sdk/selenium-webdriver-at-spi.git \
    && git fetch --depth 1 origin "${SWAS_COMMIT}" \
    && git checkout --detach FETCH_HEAD \
    && git apply /src/inputsynth-fixes.patch /src/run-output-count.patch /src/run-permission-checks.patch /src/session-attach-probe-first.patch /src/pipelined-tree.patch \
    && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=OFF \
    && cmake --build build \
    && DESTDIR=/out cmake --install build

COPY tools/krema-test-window /src/krema-test-window
RUN cmake -S /src/krema-test-window -B /src/krema-test-window/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    && cmake --build /src/krema-test-window/build \
    && DESTDIR=/out cmake --install /src/krema-test-window/build

# ctest-image: base plus ccache, without the AT-SPI stack below. A dedicated
# stage keeps the ccache install out of base's big dnf layer (a one-package
# addition would otherwise drop its gha cache entry) and out of the appium
# image, which never compiles.
FROM base AS ctest-image
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked \
    dnf -y --setopt=install_weak_deps=False --setopt=keepcache=True install \
        ccache

FROM base
COPY --from=swas-build /out/ /
# run.rb pip-installs requirements.txt unless this marker exists; everything
# is preinstalled above so skip that network round-trip on every run.
RUN touch /tmp/selenium-requirements-installed

WORKDIR /work
