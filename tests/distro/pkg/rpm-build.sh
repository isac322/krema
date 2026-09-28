#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# RPM family build (fedora, suse) using packaging/obs/krema.spec inside the
# target's base container. Invoked by in-container.sh.

set -euo pipefail

spec=/pkg/packaging/obs/krema.spec
tarball="/stage/krema-${KREMA_VERSION}.tar.gz"
topdir=/work/rpmbuild

mkdir -p "$topdir"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
cp "$spec" "$topdir/SPECS/krema.spec"
cp "$tarball" "$topdir/SOURCES/krema-${KREMA_VERSION}.tar.gz"

if [[ "$FAMILY" == "fedora" ]]; then
    dnf -y install 'dnf-command(builddep)' rpm-build tar gzip findutils \
        || dnf -y install dnf-plugins-core rpm-build tar gzip findutils
    dnf -y builddep "$topdir/SPECS/krema.spec"
elif [[ "$FAMILY" == "suse" ]]; then
    if [[ "$TARGET_ID" == "opensuse-slowroll" ]]; then
        # Same repo swap as tests/docker/Dockerfile.runtime: the base image is
        # opensuse/tumbleweed but Slowroll packages must resolve from the
        # official Slowroll repositories.
        zypper --non-interactive modifyrepo --all --disable || true
        zypper --non-interactive addrepo --refresh \
            https://download.opensuse.org/slowroll/repo/oss/ slowroll-oss
        zypper --non-interactive addrepo --refresh \
            https://download.opensuse.org/update/slowroll/repo/oss/ \
            slowroll-update
        zypper --non-interactive --gpg-auto-import-keys refresh
    else
        zypper --non-interactive refresh
    fi

    # zypper has no builddep: resolve the spec's BuildRequires as capabilities.
    # `rpmspec -P` evaluates %if suse_version so the correct ninja/ninja-build
    # alternative is picked. Version constraints are stripped; zypper takes
    # the distro's current version and cmake still enforces minimums.
    zypper --non-interactive install --no-recommends \
        rpm-build tar gzip findutils grep sed gawk
    mapfile -t buildreqs < <(
        rpmspec -P "$topdir/SPECS/krema.spec" \
            | sed -n 's/^BuildRequires:[[:space:]]*//p' \
            | cut -d' ' -f1 \
            | grep -v '^$'
    )
    zypper --non-interactive install --no-recommends "${buildreqs[@]}"
fi

rpmbuild -bb --define "_topdir $topdir" "$topdir/SPECS/krema.spec"

# Ship the binary rpm(s): krema-<version>-*.rpm, excluding debuginfo/debugsource
# (krema-debug*) and .src.rpm.
found=0
while IFS= read -r -d '' rpm; do
    cp "$rpm" /out/
    found=1
done < <(find "$topdir/RPMS" -name 'krema-[0-9]*.rpm' -print0)

if (( ! found )); then
    echo "error: rpmbuild finished but produced no krema binary rpm" >&2
    exit 70
fi
ls -l /out
