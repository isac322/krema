# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
# cloud-init NoCloud seed generation for run-vm-qa.sh.
# Expects common.sh already sourced.

# These two scripts are written verbatim into every VM image; the distro
# plug-in script is appended between them by cloud_userdata().

# Runs inside the provisioning runcmd. Everything here must be
# distro-agnostic; distro package/config differences belong to distros/*.sh
# (distro_provision) and guest/cargo-install.sh (runtime package install).
_provision_preamble() {
    cat <<'EOS'
#!/bin/bash
# Krema VM QA provisioning script (runs as root via cloud-init runcmd).
set -euxo pipefail

log() { echo "[krema-provision] $*"; }

EOS
}

# Common tail: SDDM autologin setup, qa session tweaks, serial console.
# distro_provision() output is spliced in by cloud_userdata() before this.
_provision_epilogue() {
    cat <<'EOS'

# --- SDDM: autologin qa into the Plasma Wayland session --------------------
mkdir -p /etc/sddm.conf.d
cat > /etc/sddm.conf.d/50-krema-qa-autologin.conf <<'EOF'
[Autologin]
User=qa
Session=plasma
EOF

# Order sddm after the per-boot cargo package install so krema is present
# before the session starts.
mkdir -p /etc/systemd/system/sddm.service.d
cat > /etc/systemd/system/sddm.service.d/krema-cargo.conf <<'EOF'
[Unit]
Wants=krema-cargo-install.service
After=krema-cargo-install.service
EOF

cat > /etc/systemd/system/krema-cargo-install.service <<'EOF'
[Unit]
Description=Install Krema packages from the cargo ISO
DefaultDependencies=no
After=local-fs.target network-online.target
Wants=network-online.target
Before=sddm.service
ConditionPathExists=/usr/local/sbin/krema-cargo-install.sh

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/krema-cargo-install.sh
TimeoutStartSec=600

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable krema-cargo-install.service

# --- qa session tweaks -----------------------------------------------------
# Disable screen locking/DPMS so QA and screenshots see a live session.
install -d -m 0755 -o qa -g qa /home/qa/.config
cat > /home/qa/.config/kscreenlockerrc <<'EOF'
[Daemon]
Autolock=false
LockOnResume=false
EOF
chown qa:qa /home/qa/.config/kscreenlockerrc

# Force Qt accessibility on so dock items appear in the AT-SPI tree without a
# screen reader running.
install -d -m 0755 -o qa -g qa /home/qa/.config/environment.d
cat > /home/qa/.config/environment.d/90-krema-qa-a11y.conf <<'EOF'
QT_ACCESSIBILITY=1
QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1
EOF
chown qa:qa /home/qa/.config/environment.d/90-krema-qa-a11y.conf

# Log kernel messages to the QEMU serial console so tests/vm serial.log has
# usable boot output (cloud images otherwise only log to the display).
if command -v grubby >/dev/null 2>&1; then
    grubby --update-kernel=ALL --args="console=__KREMA_SERIAL_CONSOLE__" || true
fi
if command -v restorecon >/dev/null 2>&1; then
    restorecon -R /home/qa || true
fi

touch /var/lib/krema-provisioned
sync
EOS
}

# cloud_userdata <ssh_pubkey> <distro_provision_body> <cargo_install_script>:
# emit user-data YAML. $2 is bash source embedded in krema-provision.sh
# between preamble and epilogue; __KREMA_SERIAL_CONSOLE__ is substituted with
# the host arch's serial console device. $3 is embedded as the per-boot cargo
# installer (/usr/local/sbin/krema-cargo-install.sh).
cloud_userdata() {
    local pubkey="$1" distro_body="$2" cargo_script="$3"
    [[ -f $cargo_script ]] || die "cargo install script missing: $cargo_script"
    {
        _provision_preamble
        printf '%s\n' "$distro_body"
        _provision_epilogue
    } | sed "s/__KREMA_SERIAL_CONSOLE__/$(host_serial_console)/g" > "$WORKDIR/user-provision.sh"
    chmod +x "$WORKDIR/user-provision.sh"


    cat <<EOF
#cloud-config
hostname: krema-qa
preserve_hostname: false
users:
  - name: qa
    gecos: Krema QA
    groups: [wheel]
    sudo: "ALL=(ALL) NOPASSWD:ALL"
    shell: /bin/bash
    lock_passwd: true
    ssh_authorized_keys:
      - $pubkey
ssh_pwauth: false
write_files:
  - path: /usr/local/libexec/krema-provision.sh
    permissions: '0755'
    content: |
$(sed 's/^/      /' "$WORKDIR/user-provision.sh")
  - path: /usr/local/sbin/krema-cargo-install.sh
    permissions: '0755'
    content: |
$(sed 's/^/      /' "$cargo_script")
runcmd:
  - /usr/local/libexec/krema-provision.sh
  - cloud-init status --format json > /var/log/krema-cloud-init-status.json
EOF
}

# cloud_seed <seed.iso> <user_data> <instance_id>: build a NoCloud seed ISO.
cloud_seed() {
    local seed="$1" userdata="$2" iid="$3"
    local dir
    dir="$(mktemp -d)" || return 1
    cp "$userdata" "$dir/user-data"
    printf 'instance-id: %s\nlocal-hostname: krema-qa\n' "$iid" > "$dir/meta-data"
    if have cloud-localds; then
        cloud-localds "$seed" "$dir/user-data" "$dir/meta-data"
    else
        make_iso "$seed" cidata "$dir"
    fi
    rm -rf "$dir"
}

# cargo_iso <iso> <staging_dir>: build the cargo ISO holding packages + guest
# payload under the volume label mounted by guest/cargo-install.sh.
cargo_iso() {
    local iso="$1" staging="$2"
    make_iso "$iso" KREMACARGO "$staging"
}
