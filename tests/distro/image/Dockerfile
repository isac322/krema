# syntax=docker/dockerfile:1
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Tier 3 distro E2E image: the Tier 2 runtime (tests/appium/Dockerfile) on one
# target distro, with Krema installed from a package built by
# tests/distro/build-package.sh instead of built from source. Built by
# tests/distro/run-distro-e2e.sh:
#
#   docker buildx build \
#       --build-arg BASE_IMAGE=<ref> --build-arg TARGET_ID=<id> --build-arg FAMILY=<family> \
#       --build-context appium-tools=tests/appium/tools \
#       --build-context packages=tests/distro/.cache/packages/<id> \
#       tests/distro/image
#
# FAMILY selects the package manager and package names (packages.sh sources
# <family>.sh; only that file is copied so other families stay cached).

ARG BASE_IMAGE=fedora:43
FROM ${BASE_IMAGE} AS base
ARG TARGET_ID
ARG FAMILY
# XDG_MENU_PREFIX: startplasma exports it in every Plasma session. KService
# resolves desktop files (dock item names, icons) through
# ${XDG_MENU_PREFIX}applications.menu; Debian/Ubuntu ship only
# plasma-applications.menu (Fedora also has redhat-menus' applications.menu).
ENV LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    XDG_MENU_PREFIX=plasma- \
    FAMILY=${FAMILY} \
    TARGET_ID=${TARGET_ID}
COPY packages.sh ${FAMILY}.sh /usr/local/lib/krema-distro/
RUN sh /usr/local/lib/krema-distro/packages.sh runtime

# KDE selenium-webdriver-at-spi and the test-window fixture, built on the
# target distro itself so they link against its Qt/KF libraries. Same pinned
# commit and patches as tests/appium/Dockerfile, minus the video recorder:
# entrypoint.sh never runs it (TEST_WITH_VIDEO_RECORDER=0) and it does not
# build against KPipeWire 6.3 (Ubuntu 25.04: no PipeWireRecord::start()).
FROM base AS swas-build
RUN sh /usr/local/lib/krema-distro/packages.sh build
ARG SWAS_COMMIT=d45a21e8f1b3591dc921f0be85f1ecd834cbe413
COPY --from=appium-tools inputsynth-fixes.patch run-output-count.patch run-permission-checks.patch /src/
RUN git init /src/swas && cd /src/swas \
    && git remote add origin https://invent.kde.org/sdk/selenium-webdriver-at-spi.git \
    && git fetch --depth 1 origin "${SWAS_COMMIT}" \
    && git checkout --detach FETCH_HEAD \
    && git apply /src/inputsynth-fixes.patch /src/run-output-count.patch /src/run-permission-checks.patch \
    && sed -i '/add_subdirectory(videorecorder)/d' CMakeLists.txt \
    && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=OFF \
    && cmake --build build \
    && DESTDIR=/out cmake --install build
COPY --from=appium-tools krema-test-window /src/krema-test-window
RUN cmake -S /src/krema-test-window -B /src/krema-test-window/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    && cmake --build /src/krema-test-window/build \
    && DESTDIR=/out cmake --install /src/krema-test-window/build

FROM base
COPY --from=swas-build /out/ /
# Last layer: a package rebuild only re-runs this step.
RUN --mount=type=bind,from=packages,target=/pkgs \
    sh /usr/local/lib/krema-distro/packages.sh krema
WORKDIR /work
