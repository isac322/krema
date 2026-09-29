# syntax=docker/dockerfile:1
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Tier 3 distro CI images, one pair per target. Built (and tagged with the
# tests/distro/image-ref.sh reference) by tests/distro/build-ci-image.sh:
#
#   docker buildx build --target runtime|builder \
#       --build-arg BASE_IMAGE=<ref> --build-arg TARGET_ID=<id> --build-arg FAMILY=<family> \
#       --build-context appium-tools=tests/appium/tools \
#       --build-context packaging=packaging \
#       tests/distro/image
#
# runtime: the Tier 2 runtime (tests/appium/Dockerfile) on the target distro,
#   WITHOUT krema and without krema's own runtime dependencies: the job
#   installs the package into it through the distro package manager, so the
#   package's Requires/Depends must pull those in (run-distro-e2e.sh).
# builder: the packaging toolchain, ccache and krema's build dependencies
#   (resolved from the packaging/ build context at image build time; the job
#   installs whatever a newer packaging/ adds, build-package.sh).
#
# FAMILY selects the package manager and package names (packages.sh sources
# <family>.sh; only that file is copied so other families stay cached).
# Rolling targets are fully upgraded here and again at job time
# (packages.sh upgrade).

ARG BASE_IMAGE=fedora:43
FROM ${BASE_IMAGE} AS os
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

FROM os AS runtime-base
RUN sh /usr/local/lib/krema-distro/packages.sh runtime

# KDE selenium-webdriver-at-spi and the test-window fixture, built on the
# target distro itself so they link against its Qt/KF libraries. Same pinned
# commit and patches as tests/appium/Dockerfile, minus the video recorder:
# entrypoint.sh never runs it (TEST_WITH_VIDEO_RECORDER=0) and it does not
# build against KPipeWire 6.3 (Ubuntu 25.04: no PipeWireRecord::start()).
FROM runtime-base AS swas-build
RUN sh /usr/local/lib/krema-distro/packages.sh build
ARG SWAS_COMMIT=d45a21e8f1b3591dc921f0be85f1ecd834cbe413
COPY --from=appium-tools inputsynth-fixes.patch run-output-count.patch run-permission-checks.patch session-attach-probe-first.patch pipelined-tree.patch /src/
RUN git init /src/swas && cd /src/swas \
    && git remote add origin https://invent.kde.org/sdk/selenium-webdriver-at-spi.git \
    && git fetch --depth 1 origin "${SWAS_COMMIT}" \
    && git checkout --detach FETCH_HEAD \
    && git apply /src/inputsynth-fixes.patch /src/run-output-count.patch /src/run-permission-checks.patch /src/session-attach-probe-first.patch /src/pipelined-tree.patch \
    && sed -i '/add_subdirectory(videorecorder)/d' CMakeLists.txt \
    && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=OFF \
    && cmake --build build \
    && DESTDIR=/out cmake --install build
COPY --from=appium-tools krema-test-window /src/krema-test-window
RUN cmake -S /src/krema-test-window -B /src/krema-test-window/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    && cmake --build /src/krema-test-window/build \
    && DESTDIR=/out cmake --install /src/krema-test-window/build

# Package builder: tests/distro/pkg/in-container.sh runs in it with the
# packaging/ tree mounted at the same /pkg/packaging path.
FROM os AS builder
RUN --mount=type=bind,from=packaging,target=/pkg/packaging \
    sh /usr/local/lib/krema-distro/packages.sh builder

FROM runtime-base AS runtime
COPY --from=swas-build /out/ /
WORKDIR /work
