#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# RPM family build (fedora, suse) using packaging/obs/krema.spec inside the
# target's builder image. Invoked by in-container.sh after the build deps are
# installed; rpmbuild re-checks every BuildRequires with its version.

set -euo pipefail

spec=/pkg/packaging/obs/krema.spec
tarball="/stage/krema-${KREMA_VERSION}.tar.gz"
topdir=/work/rpmbuild

mkdir -p "$topdir"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
cp "$spec" "$topdir/SPECS/krema.spec"
cp "$tarball" "$topdir/SOURCES/krema-${KREMA_VERSION}.tar.gz"

# debuginfo/debugsource stay enabled: find-debuginfo is what strips the
# binary in the shipped rpm (eu-strip + .gnu_debuglink, and .gnu_debugdata on
# Fedora), so disabling it would change the tested binary. Only the payload
# compression is lowered (Fedora defaults to zstd level 19, the slowest part
# of packaging the large debuginfo rpm): it changes how the files are
# compressed inside the .rpm, not the installed files or the dependency
# metadata the E2E suite exercises.
rpmbuild -bb --define "_topdir $topdir" --define '_binary_payload w3.zstdio' \
    "$topdir/SPECS/krema.spec"

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
