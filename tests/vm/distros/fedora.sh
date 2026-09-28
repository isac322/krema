# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
# Fedora distro plug-in for tests/vm/run-vm-qa.sh.
# Covers targets: fedora-42, fedora-43, fedora-44, fedora-rawhide.
#
# Contract with run-vm-qa.sh (sourced, TARGET_ID and FAMILY already set):
#   DISTRO_PKG_GLOB        package file glob for the cargo ISO ('*.rpm')
#   distro_image()         prints "<url> <sha256|->" of the cloud image for
#                          the host architecture
#   distro_provision()     prints bash that runs inside krema-provision.sh
#                          (root, during first boot) installing the desktop

DISTRO_PKG_GLOB='*.rpm'

_distro_rel() {
    case "$TARGET_ID" in
        fedora-rawhide) echo rawhide ;;
        fedora-*) echo "${TARGET_ID#fedora-}" ;;
        *) return 1 ;;
    esac
}

# The released images live under releases/<n>/Cloud; a not-yet-released
# branched Fedora under development/<n>/Cloud; rawhide under
# development/rawhide/Cloud. End-of-life releases move to the archive, and
# download.fedoraproject.org may redirect to a mirror that already dropped
# them, so the archive is the last fallback.
distro_image() {
    local rel arch base listing fname checksums sha
    rel="$(_distro_rel)" || die "unsupported target for fedora.sh: $TARGET_ID"
    arch="$(krema_arch)"

    local -a bases=(
        "https://download.fedoraproject.org/pub/fedora/linux/releases/$rel/Cloud"
        "https://download.fedoraproject.org/pub/fedora/linux/development/$rel/Cloud"
        "https://archives.fedoraproject.org/pub/archive/fedora/linux/releases/$rel/Cloud"
    )
    if [[ $rel == rawhide ]]; then
        bases=("https://download.fedoraproject.org/pub/fedora/linux/development/rawhide/Cloud")
    fi

    # A mirror whose CHECKSUM is unreachable only yields an unverified
    # fallback; later bases (the archive) may still provide a verified one.
    local unverified=""
    for base in "${bases[@]}"; do
        local img_dir="$base/$arch/images"
        listing="$(fetch "$img_dir/" 2>/dev/null || true)"
        [[ -n $listing ]] || continue
        # Pick the highest Fedora-Cloud-Base-Generic qcow2 for this arch.
        fname="$(printf '%s\n' "$listing" \
            | grep -oE "Fedora-Cloud-Base-Generic-${rel}-[0-9]+\.[0-9]+\.${arch}\.qcow2" \
            | version_sort | tail -n1 || true)"
        # Rawhide qcow2s have no respin suffix (Fedora-Cloud-Base-Generic-Rawhide...).
        if [[ -z $fname ]]; then
            fname="$(printf '%s\n' "$listing" \
                | grep -oE "Fedora-Cloud-Base-Generic-[A-Za-z0-9._-]*\.${arch}\.qcow2" \
                | version_sort | tail -n1 || true)"
        fi
        [[ -n $fname ]] || continue

        sha='-'
        local cf
        # Enumerate CHECKSUM files from the listing instead of globbing URLs.
        cf="$(printf '%s\n' "$listing" | grep -oE "[A-Za-z0-9._-]*${arch}[-.]CHECKSUM" | version_sort | head -n1 || true)"
        if [[ -n $cf ]]; then
            checksums="$(fetch "$img_dir/$cf" 2>/dev/null || true)"
            sha="$(printf '%s\n' "$checksums" \
                | grep -E "SHA256 \(${fname//\./\\.}\)" \
                | sed -E 's/.*= *([0-9a-f]{64}).*/\1/' | head -n1 || true)"
            sha="${sha:--}"
        fi
        if [[ $sha == - ]]; then
            unverified="${unverified:-$img_dir/$fname -}"
            continue
        fi
        echo "$img_dir/$fname $sha"
        return 0
    done
    if [[ -n $unverified ]]; then
        echo "$unverified"
        return 0
    fi
    die "no Fedora cloud image found for $TARGET_ID ($arch)"
}

# Minimal Plasma 6 Wayland + SDDM autologin + QA tooling, using Fedora package
# names. Runs as root during the first (provisioning) boot.
distro_provision() {
    cat <<'EOS'
log "installing Plasma desktop and QA tooling (dnf)"
dnf -y install --setopt=install_weak_deps=True \
    plasma-desktop \
    plasma-workspace \
    sddm \
    sddm-wayland-plasma \
    pipewire \
    pipewire-pulseaudio \
    wireplumber \
    xdg-desktop-portal-kde \
    at-spi2-core \
    at-spi2-atk \
    python3-pyatspi \
    python3-gobject \
    konsole \
    kwrite \
    google-noto-sans-fonts \
    qemu-guest-agent
dnf clean all

systemctl enable sddm.service
systemctl set-default graphical.target
EOS
}
