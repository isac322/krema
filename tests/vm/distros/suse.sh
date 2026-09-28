# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
# openSUSE distro plug-in for tests/vm/run-vm-qa.sh.
# Covers targets: opensuse-tumbleweed, opensuse-leap-16.0, opensuse-slowroll.
#
# Contract with run-vm-qa.sh (sourced, TARGET_ID and FAMILY already set):
#   DISTRO_PKG_GLOB        package file glob for the cargo ISO ('*.rpm')
#   distro_image()         prints "<url> <sha256|->" of the cloud image for
#                          the host architecture
#   distro_provision()     prints bash that runs inside krema-provision.sh
#                          (root, during first boot) installing the desktop
#
# Images are the official openSUSE "Minimal-VM ... Cloud" appliances (KIWI,
# cloud-init NoCloud, grub2 UEFI). Slowroll has no image of its own and no
# aarch64 repositories: like tests/docker/Dockerfile.runtime it boots the
# Tumbleweed x86_64 image and switches it to the Slowroll repositories
# (zypper dup) during provisioning. It is therefore CI-only (x86_64 KVM
# hosts); on aarch64 hosts distro_image() fails with an explanation.

DISTRO_PKG_GLOB='*.rpm'

# _suse_appliance_dir: download directory of the target's appliances.
# _suse_image_stem:    stable (non-versioned) image name without .qcow2.
_suse_appliance_dir() {
    local arch="$1"
    case "$TARGET_ID" in
        opensuse-tumbleweed|opensuse-slowroll)
            if [[ $arch == x86_64 ]]; then
                echo "https://download.opensuse.org/tumbleweed/appliances"
            else
                echo "https://download.opensuse.org/ports/$arch/tumbleweed/appliances"
            fi
            ;;
        opensuse-leap-*)
            echo "https://download.opensuse.org/distribution/leap/${TARGET_ID#opensuse-leap-}/appliances"
            ;;
        *) return 1 ;;
    esac
}
_suse_image_stem() {
    local arch="$1"
    case "$TARGET_ID" in
        opensuse-tumbleweed|opensuse-slowroll)
            echo "openSUSE-Tumbleweed-Minimal-VM.$arch-Cloud" ;;
        opensuse-leap-*)
            echo "Leap-${TARGET_ID#opensuse-leap-}-Minimal-VM.$arch-Cloud" ;;
        *) return 1 ;;
    esac
}

# Prefer the versioned file (…-Cloud-Snapshot<date>.qcow2 on Tumbleweed,
# …-Cloud-Build<n>.qcow2 on Leap): the stable name is a symlink that flips on
# every snapshot, so pairing it with its .sha256 can race a publish. Falls
# back to the stable name when the listing has no versioned file.
distro_image() {
    local arch dir stem listing fname sums sha
    arch="$(krema_arch)"
    if [[ $TARGET_ID == opensuse-slowroll && $arch != x86_64 ]]; then
        die "opensuse-slowroll is x86_64-only (no $arch Slowroll repositories); run it on an x86_64 KVM host (CI)"
    fi
    dir="$(_suse_appliance_dir "$arch")" || die "unsupported target for suse.sh: $TARGET_ID"
    stem="$(_suse_image_stem "$arch")"

    listing="$(fetch "$dir/" 2>/dev/null || true)"
    [[ -n $listing ]] || die "cannot list $dir/"
    local re_stem="${stem//./\\.}"
    fname="$(printf '%s\n' "$listing" \
        | grep -oE "${re_stem/-Cloud/-[0-9.]*-?Cloud}-(Snapshot|Build)[0-9.]+\.qcow2" \
        | version_sort | tail -n1 || true)"
    if [[ -z $fname ]]; then
        printf '%s\n' "$listing" | grep -qF "$stem.qcow2" \
            || die "no $stem image in $dir/"
        fname="$stem.qcow2"
    fi

    sums="$(fetch "$dir/$fname.sha256" 2>/dev/null || true)"
    sha="$(printf '%s\n' "$sums" | grep -oE '^[0-9a-f]{64}' | head -n1 || true)"
    echo "$dir/$fname ${sha:--}"
}

# Minimal Plasma 6 Wayland + SDDM (Wayland greeter) + QA tooling, using
# openSUSE package names. Runs as root during the first (provisioning) boot.
# python3-atspi/python3-gobject are capabilities provided by the primary
# python3XX-* flavor, so this survives Tumbleweed python bumps.
distro_provision() {
    if [[ $TARGET_ID == opensuse-slowroll ]]; then
        cat <<'EOS'
log "switching Tumbleweed image to the Slowroll repositories"
zypper --non-interactive modifyrepo --all --disable
zypper --non-interactive addrepo --refresh \
    https://download.opensuse.org/slowroll/repo/oss/ slowroll-oss
zypper --non-interactive addrepo --refresh \
    https://download.opensuse.org/update/slowroll/repo/oss/ slowroll-update
zypper --non-interactive --gpg-auto-import-keys refresh
zypper --non-interactive dup --allow-vendor-change
EOS
    fi
    cat <<'EOS'
log "installing Plasma desktop and QA tooling (zypper)"
zypper --non-interactive --gpg-auto-import-keys refresh
zypper --non-interactive install \
    plasma6-session \
    plasma6-desktop \
    plasma6-workspace \
    kwin6 \
    sddm-qt6 \
    pipewire \
    pipewire-pulseaudio \
    wireplumber \
    xdg-desktop-portal-kde6 \
    at-spi2-core \
    python3-atspi \
    python3-gobject \
    konsole \
    kwrite \
    google-noto-sans-fonts \
    Mesa-dri \
    qemu-guest-agent
zypper --non-interactive clean --all

# openSUSE's plasma6-session ships plasmawayland.desktop (Fedora: plasma.desktop)
# and sddm's vendor config defaults to the X11 default.desktop session. Point
# autologin at whichever Plasma Wayland session file exists; 60- sorts after
# the shared 50-krema-qa-autologin.conf, so this wins.
plasma_session=plasma
if [[ ! -f /usr/share/wayland-sessions/plasma.desktop \
      && -f /usr/share/wayland-sessions/plasmawayland.desktop ]]; then
    plasma_session=plasmawayland
fi
mkdir -p /etc/sddm.conf.d
cat > /etc/sddm.conf.d/60-krema-qa-suse.conf <<EOF
[General]
DisplayServer=wayland
GreeterEnvironment=QT_WAYLAND_SHELL_INTEGRATION=layer-shell

[Wayland]
CompositorCommand=kwin_wayland --drm --no-lockscreen --no-global-shortcuts --locale1

[Autologin]
Session=${plasma_session}.desktop
EOF

# Kernel messages on the QEMU serial console (no grubby on openSUSE).
if [[ -f /etc/default/grub ]] && ! grep -q 'console=__KREMA_SERIAL_CONSOLE__' /etc/default/grub; then
    sed -i -E 's/^(GRUB_CMDLINE_LINUX_DEFAULT=")/\1console=__KREMA_SERIAL_CONSOLE__ /' /etc/default/grub
    if command -v update-bootloader >/dev/null 2>&1; then
        update-bootloader --refresh || true
    elif command -v grub2-mkconfig >/dev/null 2>&1; then
        grub2-mkconfig -o /boot/grub2/grub.cfg || true
    fi
fi

# openSUSE images point display-manager.service at display-manager-legacy
# (driven by /etc/sysconfig/displaymanager); take the alias over for sddm.
# openSUSE's sddm is patched to take autologin from DISPLAYMANAGER_AUTOLOGIN,
# which overrides [Autologin] User= in sddm.conf.d (empty = greeter shown).
if [[ -f /etc/sysconfig/displaymanager ]]; then
    sed -i -E \
        -e 's/^DISPLAYMANAGER=.*/DISPLAYMANAGER="sddm"/' \
        -e 's/^DISPLAYMANAGER_AUTOLOGIN=.*/DISPLAYMANAGER_AUTOLOGIN="qa"/' \
        /etc/sysconfig/displaymanager
    grep -q '^DISPLAYMANAGER_AUTOLOGIN="qa"' /etc/sysconfig/displaymanager \
        || echo 'DISPLAYMANAGER_AUTOLOGIN="qa"' >> /etc/sysconfig/displaymanager
fi
systemctl disable display-manager-legacy.service 2>/dev/null || true
systemctl enable --force sddm.service
systemctl set-default graphical.target
EOS
}
