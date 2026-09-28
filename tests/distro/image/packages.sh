#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Package installation for tests/distro/image/Dockerfile. Runs inside the
# image build as root; package names live in <family>.sh next to this file.
#
#   packages.sh runtime   E2E runtime: kwin_wayland --virtual, PipeWire,
#                         AT-SPI, python deps of selenium-webdriver-at-spi and
#                         the suite, fixture apps (kwrite, kfind), fonts/icons
#   packages.sh build     builder stage only: headers/tools to build
#                         selenium-webdriver-at-spi and krema-test-window
#   packages.sh krema     final stage: installs the Krema package from /pkgs
#                         through the distro package manager, so the package's
#                         own dependency metadata has to pull in its runtime
#
# The runtime list mirrors tests/appium/Dockerfile (Fedora) minus krema's
# build dependencies, which must come from the package instead.
#
# Environment: FAMILY (fedora|suse|debian|arch), TARGET_ID.

set -eu

: "${FAMILY:?}" "${TARGET_ID:?}"

phase=${1:?usage: packages.sh runtime|build|krema}

# Python packages the distros do not ship (or ship at other versions). Pins
# follow requirements.txt of the pinned selenium-webdriver-at-spi commit, as
# in tests/appium/Dockerfile.
PIP_PACKAGES="Appium-Python-Client==4.5.1 selenium==4.29.0 pytest-timeout==2.3.1"

# Unprivileged containers have no CAP_SYS_NICE in their bounding set;
# execve() of a binary with file capability cap_sys_nice fails with EPERM.
# Also re-run after the krema install in case it pulled a kwin update.
strip_kwin_caps() {
    kwin=$(readlink -f "$(command -v kwin_wayland)")
    if [ -n "$(getcap "$kwin")" ]; then
        setcap -r "$kwin"
    fi
}

pip_install() {
    # --break-system-packages: every family marks its python EXTERNALLY-MANAGED.
    python3 -m pip install --no-cache-dir --break-system-packages $PIP_PACKAGES
    # run.rb pip-installs its requirements.txt unless this marker exists;
    # everything is preinstalled, skip that network round-trip on every run.
    touch /tmp/selenium-requirements-installed
}

# ---------------------------------------------------------------- main
case "$FAMILY" in
fedora | suse | debian | arch) ;;
*)
    echo "unknown FAMILY '$FAMILY'" >&2
    exit 64
    ;;
esac
# shellcheck source=/dev/null
. "$(dirname "$0")/$FAMILY.sh"

case "$phase" in
runtime)
    family_runtime
    strip_kwin_caps
    pip_install
    ;;
build)
    family_build
    ;;
krema)
    family_krema
    strip_kwin_caps
    command -v krema >/dev/null || { echo "package installed no krema binary in PATH" >&2; exit 1; }
    ;;
*)
    echo "unknown phase '$phase'" >&2
    exit 64
    ;;
esac
