#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Package management for the Tier 3 images (tests/distro/image/Dockerfile)
# and for the job-time steps run in containers of those images. Runs as root;
# package names live in <family>.sh next to this file.
#
#   packages.sh runtime    runtime image: kwin_wayland --virtual, PipeWire,
#                          AT-SPI, python deps of selenium-webdriver-at-spi
#                          and the suite, fixture apps (kwrite, kfind),
#                          fonts/icons
#   packages.sh build      swas-build stage only: headers/tools to build
#                          selenium-webdriver-at-spi and krema-test-window
#   packages.sh builder    builder image: packaging toolchain, ccache and the
#                          build deps of the packaging in /pkg/packaging
#   packages.sh upgrade    rolling targets only (is_rolling): full distro
#                          upgrade; a no-op elsewhere. Runs in both images
#                          and again at job time, because krema links Qt
#                          private API and a day-old image plus today's
#                          repositories would otherwise be a partial upgrade
#   packages.sh depfetch   job time, runtime container, after `upgrade`:
#                          downloads the packaging's declared runtime deps
#                          (PKGDEFS=<dir> is a copy of packaging/) so the
#                          krema install finds them in the package-manager
#                          cache. It only fills the cache and tolerates
#                          failures; the install itself still resolves
#                          every dependency
#   packages.sh builddeps  install the build deps of /pkg/packaging that are
#                          missing. The check is offline (rpm database,
#                          dpkg-checkbuilddeps, pacman -T); the package
#                          manager only runs for a missing delta
#   packages.sh krema      job time, runtime image: installs the Krema package
#                          from /pkgs through the distro package manager, so
#                          the package's own dependency metadata has to pull
#                          in its runtime
#
# The runtime list mirrors tests/appium/Dockerfile (Fedora) minus krema's
# build and runtime dependencies, which must come from the package instead.
#
# Environment: FAMILY (fedora|suse|debian|arch), TARGET_ID.

set -eu

: "${FAMILY:?}" "${TARGET_ID:?}"

phase=${1:?usage: packages.sh runtime|build|builder|upgrade|builddeps|krema}

PACKAGING=/pkg/packaging

# Python packages the distros do not ship (or ship at other versions). Pins
# follow requirements.txt of the pinned selenium-webdriver-at-spi commit, as
# in tests/appium/Dockerfile.
PIP_PACKAGES="Appium-Python-Client==4.5.1 selenium==4.29.0 pytest-timeout==2.3.1"

# Rolling releases: images are rebuilt daily, and a job-time full upgrade
# keeps the package build and the runtime on today's repository state.
is_rolling() {
    case "$TARGET_ID" in
    fedora-rawhide | opensuse-tumbleweed | opensuse-slowroll | arch) return 0 ;;
    *) return 1 ;;
    esac
}

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

# BuildRequires of krema.spec (versions stripped) that no installed package
# provides, one per line. Offline: rpm database only. Version constraints are
# still enforced by rpmbuild itself, which refuses unmet BuildRequires.
rpm_missing_buildreqs() {
    reqs=$(rpmspec -q --buildrequires "$PACKAGING/obs/krema.spec")
    for req in $(printf '%s\n' "$reqs" | awk '{ print $1 }' | sort -u); do
        rpm -q --whatprovides "$req" >/dev/null 2>&1 || echo "$req"
    done
}

# Package names the packaging declares as runtime deps, one per line, for
# depfetch to warm the package-manager cache. PKGDEFS must point at a copy
# of packaging/ (the runtime container has no mounts). Parsing is
# best-effort by design: a missed dep is still resolved and downloaded by
# the actual install.
dep_names() {
    case "$FAMILY" in
    fedora | suse)
        # Requires at file level are unconditional; inside the
        # `0%{?suse_version}` conditional the %if side is openSUSE's, the
        # %else side Fedora's. Only the first word (the package name) is
        # kept.
        awk -v suse=$([ "$FAMILY" = suse ] && echo 1 || echo 0) '
            /^%if/ { s++; in_suse = $0 ~ /suse_version/; next }
            /^%else/ { if (s == 1) { in_else = 1; in_suse = 0 } next }
            /^%endif/ { s--; if (s == 0) { in_suse = 0; in_else = 0 } next }
            /^Requires:/ {
                keep = suse ? (s == 0 || in_suse) : (s == 0 || in_else)
                if (keep) { name = $2; gsub(/%.*/, "", name); if (name != "") print name }
            }
        ' "$PKGDEFS/obs/krema.spec"
        ;;
    debian)
        # Depends: block of the binary stanza; first name of each
        # comma-separated alternative, ${...} substitutions skipped.
        awk '
            /^[A-Z]/ { indep = ($1 == "Depends:") }
            indep {
                line = $0; sub(/^[A-Za-z-]+:[ \t]*/, "", line)
                n = split(line, a, ",")
                for (i = 1; i <= n; i++) {
                    name = a[i]
                    sub(/^[ \t]+/, "", name)
                    sub(/[ \t].*$/, "", name)
                    if (name !~ /^\$\{/ && name != "") print name
                }
            }
        ' "$PKGDEFS/obs/debian.control"
        ;;
    arch)
        sh -c '. "$1"; printf "%s\n" "${depends[@]}"' _ "$PKGDEFS/arch/PKGBUILD" | sed 's/[<>=].*//'
        ;;
    esac | sort -u
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
    if is_rolling; then family_upgrade; fi
    family_runtime
    strip_kwin_caps
    pip_install
    ;;
build)
    family_build
    ;;
builder)
    if is_rolling; then family_upgrade; fi
    family_toolchain
    family_builddeps
    family_clean
    ;;
upgrade)
    if is_rolling; then
        family_upgrade
    else
        echo "packages.sh: $TARGET_ID is not rolling, no upgrade"
    fi
    ;;
builddeps)
    family_builddeps
    ;;
depfetch)
    names=$(dep_names)
    if [ -n "$names" ]; then
        # shellcheck disable=SC2086
        family_depfetch $names || \
            echo "packages.sh: depfetch incomplete on $TARGET_ID, install will download the rest"
    fi
    ;;
krema)
    family_krema
    strip_kwin_caps
    # A rolling upgrade to a new python minor leaves the pip packages in the
    # old site-packages directory.
    python3 -c 'import appium, selenium, pytest_timeout' 2>/dev/null || pip_install
    command -v krema >/dev/null || { echo "package installed no krema binary in PATH" >&2; exit 1; }
    ;;
*)
    echo "unknown phase '$phase'" >&2
    exit 64
    ;;
esac
