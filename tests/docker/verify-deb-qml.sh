#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# verify-deb-qml.sh — clean-base regression gate for the Debian/Ubuntu .deb.
#
# Contract: every QML module imported under src/qml/ (the app's own
# com.bhyoo.krema module excluded) must be resolvable by the package's own
# Depends. The gate proves this by installing the built .deb into clean base
# images with --no-install-recommends and checking module presence + loadability.
#
# This is intentionally NOT run in tests/docker/Dockerfile.runtime: that image
# pre-installs runtime dependencies, which masks missing Depends entries.
#
# Usage:
#   verify-deb-qml.sh <target|all> <package-dir>
#   verify-deb-qml.sh --list-imports
#
# Targets (clean base images):
#   ubuntu-25.04  ubuntu:25.04
#   ubuntu-25.10  ubuntu:25.10
#   ubuntu-26.04  ubuntu:26.04
#   debian-13     debian:13
#
# Exit status: 0 if every selected target passes, 1 otherwise, 64 on usage error.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

declare -A BASE_IMAGES=(
    [ubuntu-25.04]="ubuntu:25.04"
    [ubuntu-25.10]="ubuntu:25.10"
    [ubuntu-26.04]="ubuntu:26.04"
    [debian-13]="debian:13"
)

# Derive the set of external QML module imports used under src/qml/.
# Strips version numbers and `as` aliases, excludes the app's own module and
# relative/directory imports.
# Known blind spot (documented, deliberate): Qt.createComponent("<module>", ...)
# dynamic loads are not derived. org.kde.kirigamiaddons.formcard is loaded
# this way (SettingsDialog.qml) but is also statically imported under
# settings/, so it stays covered.
derive_imports() {
    grep -rhoE '^[[:space:]]*import[[:space:]]+[A-Za-z0-9_.]+' "${repo_root}/src/qml" \
        | awk '{print $2}' \
        | grep -vx 'com\.bhyoo\.krema' \
        | sort -u
}

if [[ "${1:-}" == "--list-imports" ]]; then
    derive_imports
    exit 0
fi

# Static packaging invariants that do not need a container:
#  - debian.changelog parses and its head version matches krema.dsc Version
#    (OBS debtransform consumes the .dsc; drift aborts the OBS build).
if [[ "${1:-}" == "--check-packaging" ]]; then
    command -v dpkg-parsechangelog > /dev/null 2>&1 \
        || { echo "--check-packaging requires dpkg-parsechangelog (run on a Debian/Ubuntu host, or in a container — bare debian:13 has no dpkg-dev, so install it first: docker run --rm -v \"${repo_root}:/src\" debian:13 bash -c 'apt-get update -qq && apt-get install -y -qq dpkg-dev && bash /src/tests/docker/verify-deb-qml.sh --check-packaging')" >&2; exit 64; }
    fail=0
    chlog_ver="$(dpkg-parsechangelog -l "${repo_root}/packaging/obs/debian.changelog" -S Version 2>/dev/null)" \
        || { echo "debian.changelog does not parse"; fail=1; }
    dsc_ver="$(awk '/^Version:/ {print $2; exit}' "${repo_root}/packaging/obs/krema.dsc")"
    if [[ "${fail}" -eq 0 && "${chlog_ver}" != "${dsc_ver}" ]]; then
        echo "debian.changelog head (${chlog_ver}) != krema.dsc Version (${dsc_ver})"
        fail=1
    fi
    [[ "${fail}" -eq 0 ]] && echo "check-packaging: OK (version ${chlog_ver})"
    exit "${fail}"
fi

target="${1:-}"
package_dir="${2:-}"
if [[ -z "${target}" || -z "${package_dir}" ]]; then
    echo "Usage: $0 <target|all> <package-dir>" >&2
    echo "       $0 --list-imports | --check-packaging" >&2
    echo "Targets: ${!BASE_IMAGES[*]} | all" >&2
    exit 64
fi
if [[ "${target}" == "all" ]]; then
    targets=(ubuntu-25.04 ubuntu-25.10 ubuntu-26.04 debian-13)
else
    [[ -n "${BASE_IMAGES[${target}]:-}" ]] || { echo "Unknown target: ${target}" >&2; exit 64; }
    targets=("${target}")
fi
[[ -d "${package_dir}" ]] || { echo "package dir not found: ${package_dir}" >&2; exit 64; }
compgen -G "${package_dir}/krema_*.deb" > /dev/null || { echo "no krema_*.deb in ${package_dir}" >&2; exit 64; }
package_dir="$(cd -- "${package_dir}" && pwd)"

imports="$(derive_imports)"
[[ -n "${imports}" ]] || { echo "derived zero QML imports from ${repo_root}/src/qml" >&2; exit 64; }
echo "## imports under test:"
echo "${imports}" | sed 's/^/##   /'


# Build the container-side gate assets in a temp dir mounted read-only.
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

printf '%s\n' "${imports}" > "${work}/imports.txt"

{
    echo "import QtQuick"
    while IFS= read -r mod; do
        # QtQuick already emitted; skip duplicates, keep the rest verbatim.
        [[ "${mod}" == "QtQuick" ]] && continue
        echo "import ${mod}"
    done <<< "${imports}"
    # Quit once the component (and therefore every import) has loaded, so a
    # healthy probe exits 0 instead of idling in the event loop until timeout.
    echo 'Item { Component.onCompleted: Qt.quit() }'
} > "${work}/probe.qml"

cat > "${work}/gate-inner.sh" <<'INNER'
#!/usr/bin/env bash
# Runs inside the clean base image. Fails (non-zero) if any derived QML
# module is not installed by the package's own Depends.
set -uo pipefail
export DEBIAN_FRONTEND=noninteractive
. /etc/os-release
echo "## target=${PRETTY_NAME} arch=$(dpkg --print-architecture)"

gate_fail() { echo "$1"; echo "GATE: FAIL"; exit 1; }

# Exactly one krema_*.deb is required: dpkg-buildpackage writes to the parent
# dir, so a package dir accumulating respins can contain several builds and a
# silent alphabetical pick would audit a stale .deb and misreport.
debs=( /packages/krema_*.deb )
[[ -f "${debs[0]:-}" ]] || gate_fail "no krema_*.deb in /packages"
[[ ${#debs[@]} -eq 1 ]] || gate_fail "expected exactly one krema_*.deb in /packages, found ${#debs[@]}: ${debs[*]}"
deb="${debs[0]}"
echo "## deb=$(basename "${deb}")"

apt-get update -qq || gate_fail "apt-get update failed"
apt-get install -y --no-install-recommends "${deb}" || gate_fail "INSTALL: FAIL (package Depends not installable)"
status="$(dpkg-query -W -f='${Status}' krema 2>/dev/null)"
[[ "${status}" == "install ok installed" ]] || gate_fail "INSTALL: FAIL (krema status: '${status}')"
echo "INSTALL: OK $(dpkg-query -W -f='${Package} ${Version} ${Architecture}' krema)"

fail=0
echo "--- stage 1: qmldir presence audit (no runner installed yet)"
while IFS= read -r mod; do
    rel="${mod//.//}"
    if ls /usr/lib/*/qt6/qml/"${rel}"/qmldir >/dev/null 2>&1 \
       || ls /usr/lib/qt6/qml/"${rel}"/qmldir >/dev/null 2>&1; then
        echo "FS ${mod}: present"
    else
        echo "FS ${mod}: MISSING"
        fail=1
    fi
done < /gate/imports.txt

if [[ "${fail}" -eq 0 ]]; then
    echo "--- stage 2: offscreen QML load probe"
    # The qml runner is a test harness installed AFTER the audit; it can only
    # reduce coverage, never create a false pass of stage 1.
    # qml-qt6 ships the qml6 runner on Debian 13 and Ubuntu 25.04-26.04; a
    # missing runner is a gate failure, never a silent skip.
    apt-get install -y -qq --no-install-recommends qml-qt6 qt6-qpa-plugins \
        || gate_fail "PROBE: FAIL (cannot install qml-qt6 runner)"
    qml_bin="$(command -v qml6 || ls /usr/lib/qt6/bin/qml 2>/dev/null || true)"
    [[ -n "${qml_bin}" ]] || gate_fail "PROBE: FAIL (qml runner not found after installing qml-qt6)"
    echo "runner=${qml_bin}"
    out="$(QT_QPA_PLATFORM=offscreen timeout 60 "${qml_bin}" /gate/probe.qml 2>&1)"
    rc=$?
    echo "${out}" | grep -vE '^$' | head -10
    if [[ ${rc} -ne 0 ]] || grep -q 'is not installed' <<< "${out}"; then
        echo "PROBE: FAIL rc=${rc}"
        fail=1
    else
        echo "PROBE: OK"
    fi
fi

if [[ "${fail}" -eq 0 ]]; then
    echo "GATE: PASS"
else
    echo "GATE: FAIL"
fi
exit "${fail}"
INNER
chmod +x "${work}/gate-inner.sh"

overall=0
for t in "${targets[@]}"; do
    image="${BASE_IMAGES[${t}]}"
    echo ""
    echo "===== ${t} (${image}) ====="
    if docker run --rm -i --label krema-deb-qml-gate \
        -v "${work}":/gate:ro \
        -v "${package_dir}":/packages:ro \
        "${image}" bash /gate/gate-inner.sh; then
        echo "===== ${t}: PASS ====="
    else
        echo "===== ${t}: FAIL ====="
        overall=1
    fi
done
exit "${overall}"
