# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
# Debian-family distro plug-in for tests/vm/run-vm-qa.sh.
# Covers targets: debian-13, ubuntu-25.04, ubuntu-25.10, ubuntu-26.04.
#
# Contract with run-vm-qa.sh (sourced, TARGET_ID and FAMILY already set):
#   DISTRO_PKG_GLOB        package file glob for the cargo ISO ('*.deb')
#   distro_image()         prints "<url> <sha256|sha512:hex|->" of the cloud
#                          image for the host architecture
#   distro_provision()     prints bash that runs inside krema-provision.sh
#                          (root, during first boot) installing the desktop

DISTRO_PKG_GLOB='*.deb'

# Ubuntu release -> codename (cloud-images.ubuntu.com is keyed by codename).
_ubuntu_codename() {
    case "$1" in
        25.04) echo plucky ;;
        25.10) echo questing ;;
        26.04) echo resolute ;;
        *) return 1 ;;
    esac
}

# _sums_lookup <sums-text> <filename>: hex digest for <filename> from a
# "<hex> [*]<name>" checksum list.
_sums_lookup() {
    printf '%s\n' "$1" | awk -v f="$2" '{n=$2; sub(/^\*/, "", n)} n == f {print $1; exit}'
}

distro_image() {
    local arch sums sha
    arch="$(krema_arch_deb)"
    case "$TARGET_ID" in
        debian-*)
            # Debian publishes SHA512SUMS only. Use the 'generic' image, not
            # 'genericcloud': the latter boots linux-image-cloud-*, which has
            # no virtio_gpu/DRM driver, so seat0 never becomes graphical and
            # SDDM never starts a session.
            local rel="${TARGET_ID#debian-}" codename base fname
            case "$rel" in
                13) codename=trixie ;;
                *) die "unsupported target for debian.sh: $TARGET_ID" ;;
            esac
            base="https://cloud.debian.org/images/cloud/$codename/latest"
            fname="debian-$rel-generic-$arch.qcow2"
            sums="$(fetch "$base/SHA512SUMS")" || die "cannot fetch $base/SHA512SUMS"
            sha="$(_sums_lookup "$sums" "$fname")"
            [[ -n $sha ]] || die "$fname not listed in $base/SHA512SUMS"
            echo "$base/$fname sha512:$sha"
            ;;
        ubuntu-*)
            local ver="${TARGET_ID#ubuntu-}" codename base fname
            codename="$(_ubuntu_codename "$ver")" \
                || die "unsupported target for debian.sh: $TARGET_ID"
            # Supported releases live under <codename>/current/; once a release
            # is EOL that tree is removed and only releases/<codename>/release/
            # (with versioned file names) remains.
            local -a cands=(
                "https://cloud-images.ubuntu.com/$codename/current $codename-server-cloudimg-$arch.img"
                "https://cloud-images.ubuntu.com/releases/$codename/release ubuntu-$ver-server-cloudimg-$arch.img"
            )
            local c
            for c in "${cands[@]}"; do
                base="${c%% *}" fname="${c##* }"
                sums="$(fetch "$base/SHA256SUMS" 2>/dev/null)" || continue
                sha="$(_sums_lookup "$sums" "$fname")"
                [[ -n $sha ]] || continue
                echo "$base/$fname $sha"
                return 0
            done
            die "no Ubuntu cloud image found for $TARGET_ID ($arch)"
            ;;
        *) die "unsupported target for debian.sh: $TARGET_ID" ;;
    esac
}

# Minimal Plasma 6 Wayland + SDDM + QA tooling, Debian/Ubuntu package names.
# --no-install-recommends keeps the install to ~800 packages (vs ~1400 with
# Recommends); almost every runtime piece the session needs (qt6-wayland, mesa,
# kactivitymanagerd, plasma-integration, ...) is a hard dependency. The
# Recommends a real desktop install would get and the session needs are listed
# explicitly (plasma-session-wayland, mesa-vulkan-drivers).
# Runs as root during the first (provisioning) boot.
distro_provision() {
    cat <<'EOS'
export DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a

# Background apt jobs (apt-daily, unattended-upgrades) would race both this
# install and the per-boot krema install (plain apt-get, no lock wait).
systemctl disable --now apt-daily.timer apt-daily-upgrade.timer 2>/dev/null || true
systemctl mask unattended-upgrades.service 2>/dev/null || true

log "installing Plasma desktop and QA tooling (apt)"
apt_opts=(-o DPkg::Lock::Timeout=900 -o Acquire::Retries=3)
apt-get "${apt_opts[@]}" update
# Ubuntu >= 25.10 moved the Wayland session file (wayland-sessions/plasma.desktop)
# out of plasma-workspace into plasma-session-wayland (only a Recommends there).
extra=()
if apt-cache show plasma-session-wayland >/dev/null 2>&1; then
    extra+=(plasma-session-wayland)
fi
# mesa-vulkan-drivers (lavapipe; libvulkan1 Recommends it): without a
# Vulkan ICD, Mesa 25.0 (Ubuntu 25.04) deadlocks in driCreateNewScreen3 after
# its zink fallback fails on virtio-gpu (no virgl), hanging every Qt client
# that creates a GL context (kded6, plasmashell, krema) at startup.
apt-get "${apt_opts[@]}" install -y --no-install-recommends "${extra[@]}" \
    plasma-workspace \
    plasma-desktop \
    kwin-wayland \
    sddm \
    sddm-theme-breeze \
    dbus-user-session \
    pipewire \
    pipewire-pulse \
    wireplumber \
    xdg-desktop-portal-kde \
    polkit-kde-agent-1 \
    at-spi2-core \
    libatk-adaptor \
    python3-pyatspi \
    python3-gi \
    konsole \
    kwrite \
    breeze-icon-theme \
    fonts-noto-core \
    systemd-coredump \
    qemu-guest-agent \
    mesa-vulkan-drivers
apt-get clean
test -f /usr/share/wayland-sessions/plasma.desktop \
    || { log "no Plasma Wayland session file installed"; exit 1; }

systemctl enable sddm.service
systemctl set-default graphical.target

# Kernel messages on the QEMU serial port (no grubby on Debian/Ubuntu).
if command -v update-grub >/dev/null 2>&1; then
    mkdir -p /etc/default/grub.d
    echo 'GRUB_CMDLINE_LINUX_DEFAULT="$GRUB_CMDLINE_LINUX_DEFAULT console=tty0 console=__KREMA_SERIAL_CONSOLE__"' \
        > /etc/default/grub.d/90-krema-qa-serial.cfg
    update-grub || true
fi
EOS
}
