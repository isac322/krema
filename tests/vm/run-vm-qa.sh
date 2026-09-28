#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Tier 3 VM QA: boot a real distro cloud image (Plasma 6 Wayland + SDDM
# autologin), install the Krema package built by tests/vm/build-package.sh,
# and verify krema inside the running session over SSH.
#
#   tests/vm/run-vm-qa.sh <target-id> <package-dir> [--interactive]
#
# <target-id> is a row of tests/docker/targets.tsv (fedora-43, debian-13,
# opensuse-tumbleweed, ubuntu-26.04, ...) or 'arch'. <package-dir> contains
# the package file(s) matching the target's package glob (*.rpm, *.deb,
# *.pkg.tar.zst).
#
# With --interactive the VM stays up with a VNC display on localhost for
# manual QA; otherwise the guest verifier runs and artifacts land in
# tests/vm/artifacts/<target>/.
#
# Cache layout (gitignored, tests/vm/.cache/<target>/):
#   base.qcow2     downloaded cloud image (sha256-verified when available)
#   golden.qcow2   post-provision image (Plasma installed, krema NOT yet)
#   golden.key     fingerprint of (provision script, cargo installer, image
#                  sha); a mismatch triggers a fresh provision. A changed
#                  *package* never invalidates the golden image: it is
#                  reinstalled on every boot by krema-cargo-install.service.
#   seed.iso       stable NoCloud seed (same instance-id on every boot)
#   id_ed25519*    SSH key injected into the golden image
#
# Environment:
#   KREMA_VM_CACHE              cache root      (default tests/vm/.cache)
#   KREMA_VM_ARTIFACTS          artifacts root  (default tests/vm/artifacts)
#   KREMA_VM_MEM / _SMP / _HEADS / _DISK_GB    4096 / 4 / 1 / 14
#   KREMA_VM_PROVISION_TIMEOUT  seconds         (default 3600)
#   KREMA_VM_BOOT_TIMEOUT       seconds         (default 900)
#   KREMA_VM_VERIFY_TIMEOUT     per-wait seconds in the guest (default 60)
#   KREMA_VM_APP / KREMA_VM_APP_DESKTOP / KREMA_VM_APP_REGEX
#                                             kwrite / org.kde.kwrite / ...
#
# Host requirements: qemu-system-<arch>, qemu-img, an ISO tool
# (cloud-localds or genisoimage/xorriso), UEFI firmware (qemu-efi-aarch64 on
# arm64, ovmf on x86_64), curl, ssh/scp/ssh-keygen, python3, /dev/kvm.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "$script_dir/lib/common.sh"
# shellcheck source=lib/cloud-init.sh
source "$script_dir/lib/cloud-init.sh"
# shellcheck source=lib/qemu.sh
source "$script_dir/lib/qemu.sh"

usage() { sed -n '5,44p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

interactive=0
positional=()
while (($#)); do
    case "$1" in
        --interactive) interactive=1; shift ;;
        -h|--help) usage; exit 0 ;;
        -*) die "unknown option: $1" 64 ;;
        *) positional+=("$1"); shift ;;
    esac
done
if ((${#positional[@]} < 2)); then usage >&2; exit 64; fi
TARGET_ID="${positional[0]}"
PACKAGE_DIR="${positional[1]}"
[[ -d $PACKAGE_DIR ]] || die "package dir not found: $PACKAGE_DIR" 66
PACKAGE_DIR="$(cd "$PACKAGE_DIR" && pwd)"

CACHE="${KREMA_VM_CACHE:-$script_dir/.cache}"
ARTIFACTS_ROOT="${KREMA_VM_ARTIFACTS:-$script_dir/artifacts}"
VM_MEM="${KREMA_VM_MEM:-4096}"
VM_SMP="${KREMA_VM_SMP:-4}"
VM_HEADS="${KREMA_VM_HEADS:-1}"
VM_DISK_GB="${KREMA_VM_DISK_GB:-14}"
PROVISION_TIMEOUT="${KREMA_VM_PROVISION_TIMEOUT:-3600}"
BOOT_TIMEOUT="${KREMA_VM_BOOT_TIMEOUT:-900}"
VERIFY_TIMEOUT="${KREMA_VM_VERIFY_TIMEOUT:-60}"
QA_APP="${KREMA_VM_APP:-kwrite}"
QA_APP_DESKTOP="${KREMA_VM_APP_DESKTOP:-org.kde.kwrite}"
QA_APP_REGEX="${KREMA_VM_APP_REGEX:-(?i)kwrite|untitled}"

# ---------------------------------------------------------------------------
# target resolution

TARGETS_FILE="$(cd "$script_dir/../docker" && pwd)/targets.tsv"
FAMILY="" PKG_GLOB=""
if [[ -f $TARGETS_FILE ]]; then
    while IFS=$'\t' read -r row_target row_family _img _dig _repo row_glob; do
        if [[ $row_target == "$TARGET_ID" ]]; then
            FAMILY="$row_family"; PKG_GLOB="$row_glob"
        fi
    done < "$TARGETS_FILE"
fi
# 'arch' is not in the Docker matrix; map it explicitly.
if [[ -z $FAMILY && $TARGET_ID == arch ]]; then
    FAMILY=arch; PKG_GLOB='*.pkg.tar.zst'
fi
[[ -n $FAMILY ]] || die "unknown target '$TARGET_ID' (not in $TARGETS_FILE)"

DISTRO_SCRIPT="$script_dir/distros/$FAMILY.sh"
[[ -f $DISTRO_SCRIPT ]] || die "no VM distro plug-in yet: tests/vm/distros/$FAMILY.sh
(known families: $(awk -F '\t' 'NF>1 {print $2}' "$TARGETS_FILE" | sort -u | tr '\n' ' ')arch)"
# shellcheck source=distros/fedora.sh
source "$DISTRO_SCRIPT"
PKG_GLOB="${DISTRO_PKG_GLOB:-$PKG_GLOB}"

require_tools
host_uefi_detect

CACHE_T="$CACHE/$TARGET_ID"
ARTIFACTS="$ARTIFACTS_ROOT/$TARGET_ID"
mkdir -p "$CACHE_T" "$ARTIFACTS"
WORKDIR="$(mktemp -d "$ARTIFACTS/.work.XXXXXX")"
SSH_PORT="$(free_port)"
VNC_DISPLAY="$(free_vnc_display)"
VNC_PORT=$((5900 + VNC_DISPLAY))
SSH_KEY="$CACHE_T/id_ed25519"
SEED_ISO="$CACHE_T/seed.iso"
QMP_SOCK="$WORKDIR/qmp.sock"
SERIAL_LOG="$ARTIFACTS/serial.log"
EFI_VARS="$WORKDIR/efivars.fd"
QEMU_PID=""
TIMINGS=()
CARGO_INSTALLER="$script_dir/guest/cargo-install.sh"

cleanup() {
    if [[ -n $QEMU_PID ]] && pid_alive "$QEMU_PID"; then
        kill "$QEMU_PID" 2>/dev/null || true
        for _ in {1..20}; do pid_alive "$QEMU_PID" || break; sleep 0.5; done
        kill -9 "$QEMU_PID" 2>/dev/null || true
    fi
    rm -rf "$WORKDIR"
}
trap cleanup EXIT

# boot_vm <disk> [cargo.iso]: start QEMU daemonized; sets QEMU_PID.
boot_vm() {
    local disk="$1" cargo="${2:-}"
    prepare_efi_vars "$EFI_VARS"
    vm_qemu_args "$SSH_PORT" "$VNC_DISPLAY" "$disk" \
        "$SEED_ISO" "$cargo" "$SERIAL_LOG" "$QMP_SOCK" "$WORKDIR/qemu.pid" \
        "$VM_MEM" "$VM_SMP" "$VM_HEADS" "$EFI_VARS"
    log "booting ${QEMU_ARGS[0]} (ssh 127.0.0.1:$SSH_PORT, vnc 127.0.0.1:$VNC_PORT)"
    "${QEMU_ARGS[@]}"
    QEMU_PID="$(cat "$WORKDIR/qemu.pid")"
    log "qemu pid $QEMU_PID"
}

wait_vm_exit() {
    local deadline=$((SECONDS + ${1:-120}))
    while ((SECONDS < deadline)); do
        [[ -z $QEMU_PID ]] && return 0
        pid_alive "$QEMU_PID" || return 0
        sleep 2
    done
    return 1
}

dump_guest_journal() {
    ssh_qa "$SSH_PORT" "$SSH_KEY" \
        'sudo -n journalctl -b --no-pager -o short-precise 2>/dev/null || journalctl -b --no-pager 2>/dev/null || true' \
        > "$ARTIFACTS/journal.txt" 2>/dev/null || true
    ssh_qa "$SSH_PORT" "$SSH_KEY" \
        'sudo -n journalctl -u krema-cargo-install.service -u sddm.service -u krema-provision -b --no-pager 2>/dev/null || true' \
        > "$ARTIFACTS/boot-services.log" 2>/dev/null || true
}

screendump() {
    qmp_screendump "$QMP_SOCK" "$ARTIFACTS/screen.png" \
        || qmp_screendump "$QMP_SOCK" "$ARTIFACTS/screen.ppm" || true
    if [[ -f $ARTIFACTS/screen.png || -f $ARTIFACTS/screen.ppm ]]; then
        log "screenshot saved under $ARTIFACTS"
    else
        warn "screendump failed"
    fi
}

record_time() { TIMINGS+=("$1=$2"); }

# ---------------------------------------------------------------------------
# 1. resolve cloud image (URL + sha256; '-' when the distro has none)

timer_start
resolution_failed=0
IMAGE_LINE="$(distro_image 2>"$WORKDIR/distro_image.err")" || resolution_failed=1
IMAGE_URL="" IMAGE_SHA="-"
if ((resolution_failed == 0)); then
    IMAGE_URL="${IMAGE_LINE%% *}"
    IMAGE_SHA="${IMAGE_LINE##* }"
fi
record_time image_resolve "$(timer_stop)"
log "image: ${IMAGE_URL:-unresolved} sha256=${IMAGE_SHA}"

# ---------------------------------------------------------------------------
# 2. ssh key (per cache dir, so it survives as long as the golden image)

if [[ ! -f $SSH_KEY ]]; then
    ssh-keygen -q -t ed25519 -N '' -C "krema-vm-qa-$TARGET_ID" -f "$SSH_KEY"
fi
SSH_PUBKEY="$(cat "$SSH_KEY.pub")"

# ---------------------------------------------------------------------------
# 3. golden image: provision or reuse

GOLDEN="$CACHE_T/golden.qcow2"
BASE="$CACHE_T/base.qcow2"
GOLDEN_KEY="$CACHE_T/golden.key"

gen_provision_script() {
    cloud_userdata "$SSH_PUBKEY" "$(distro_provision)" "$CARGO_INSTALLER" \
        > "$WORKDIR/user-data"
    # fingerprint inputs: provision script + cargo installer + image sha
    cat "$WORKDIR/user-provision.sh" "$CARGO_INSTALLER" > "$WORKDIR/fp.in"
    echo "$IMAGE_SHA" >> "$WORKDIR/fp.in"
    sha256_of "$WORKDIR/fp.in"
}

provision_needed=1
if [[ -f $GOLDEN && -f $GOLDEN_KEY && -f $SEED_ISO ]]; then
    if ((resolution_failed)); then
        provision_needed=0
        warn "cannot resolve the cloud image offline; reusing cached golden image"
    else
        gen_provision_script > /dev/null  # leaves user-provision.sh for fp
        fp="$(sha256_of "$WORKDIR/fp.in")"
        if [[ "$(cat "$GOLDEN_KEY")" == "$fp" ]]; then
            provision_needed=0
            log "golden image cache hit (fingerprint ${fp:0:16}…)"
        else
            log "provision inputs changed; reprovisioning"
        fi
    fi
fi

if ((provision_needed)); then
    ((resolution_failed == 0)) \
        || die "cannot provision: $(cat "$WORKDIR/distro_image.err" 2>/dev/null || echo 'image resolution failed')"

    # 3a. base image (cached; re-downloaded when the resolved sha changed)
    timer_start
    # base.qcow2 is a converted copy; base.sha256 records which upstream image
    # it came from (qemu-img rewrite changes the file hash).
    if [[ ! -f $BASE || ! -f $BASE.sha256 ]] \
        || [[ $IMAGE_SHA != "-" && "$(cat "$BASE.sha256")" != "$IMAGE_SHA" ]] \
        || [[ $IMAGE_SHA == "-" && "$(cat "$BASE.sha256")" != "$IMAGE_URL" ]]; then
        log "downloading $IMAGE_URL"
        download "$IMAGE_URL" "$WORKDIR/image.download"
        if [[ $IMAGE_SHA != "-" ]]; then
            actual="$(image_digest_of "$IMAGE_SHA" "$WORKDIR/image.download")"
            [[ $actual == "$IMAGE_SHA" ]] \
                || die "image checksum mismatch: $actual != $IMAGE_SHA"
        fi
        qemu-img convert -O qcow2 "$WORKDIR/image.download" "$BASE.new"
        mv "$BASE.new" "$BASE"
        printf '%s' "${IMAGE_SHA/\-/$IMAGE_URL}" > "$BASE.sha256"
        rm -f "$WORKDIR/image.download"
    fi
    record_time image_download "$(timer_stop)"

    # 3b. provision disk: private copy resized for the desktop install
    timer_start
    rm -f "$CACHE_T/prov.qcow2"
    qemu-img convert -O qcow2 "$BASE" "$CACHE_T/prov.qcow2"
    qemu-img resize "$CACHE_T/prov.qcow2" "${VM_DISK_GB}G"

    gen_provision_script
    iid="krema-qa-$TARGET_ID"
    cloud_seed "$SEED_ISO" "$WORKDIR/user-data" "$iid"

    boot_vm "$CACHE_T/prov.qcow2"   # no cargo ISO on the provision boot
    if ! wait_for_ssh "$SSH_PORT" "$SSH_KEY" "$PROVISION_TIMEOUT"; then
        dump_guest_journal; screendump
        die "provisioning: SSH never came up within ${PROVISION_TIMEOUT}s (see $SERIAL_LOG)"
    fi
    log "ssh up; waiting for cloud-init provisioning to finish"
    prov_deadline=$((SECONDS + PROVISION_TIMEOUT))
    prov_done=0
    while ((SECONDS < prov_deadline)); do
        if ssh_qa "$SSH_PORT" "$SSH_KEY" \
            'test -f /var/lib/krema-provisioned || exit 1' 2>/dev/null; then
            prov_done=1; break
        fi
        # bail early only on a *real* failure: cloud-final.service failed, or
        # cloud-init's top-level status is "error". Recoverable warnings
        # (hostname, ...) must not trip this.
        status="$(ssh_qa "$SSH_PORT" "$SSH_KEY" \
            'echo "unit=$(systemctl is-failed cloud-final.service 2>/dev/null || true)"; \
             { cloud-init status --format json 2>/dev/null || cloud-init status 2>/dev/null || true; }' \
            2>/dev/null || true)"
        if [[ $status == *unit=failed* ]] \
            || [[ $status == *'"status": "error"'* ]] \
            || [[ $status == *'"status":"error"'* ]] \
            || [[ $status == "status: error"* ]]; then
            break
        fi
        # cloud-init finished but the provision script never reached its
        # marker: runcmd failed (some cloud-init versions still report
        # "done" with no errors). Re-check the marker to close the race with
        # a just-finished script, then fail instead of idling to the timeout.
        if [[ $status == *'"status": "done"'* ]] \
            || [[ $status == *'"status":"done"'* ]] \
            || [[ $status == *"status: done"* ]]; then
            if ssh_qa "$SSH_PORT" "$SSH_KEY" \
                'test -f /var/lib/krema-provisioned || exit 1' 2>/dev/null; then
                prov_done=1
            fi
            break
        fi
        sleep 10
    done
    if ((prov_done == 0)); then
        dump_guest_journal; screendump
        die "provisioning failed or timed out (cloud-init status: ${status:-unknown}; see $ARTIFACTS)"
    fi
    ssh_qa "$SSH_PORT" "$SSH_KEY" 'cloud-init status --format json' \
        > "$ARTIFACTS/provision-cloudinit.json" 2>/dev/null || true
    record_time provision "$(timer_stop)"

    ssh_qa "$SSH_PORT" "$SSH_KEY" 'sudo -n poweroff' || true
    wait_vm_exit 240 || warn "qemu did not exit after poweroff; killing"
    QEMU_PID=""
    mv "$CACHE_T/prov.qcow2" "$GOLDEN"
    sha256_of "$WORKDIR/fp.in" > "$GOLDEN_KEY"
    log "golden image ready: $GOLDEN"
    [[ -f $SERIAL_LOG ]] && cp "$SERIAL_LOG" "$ARTIFACTS/provision-serial.log"
fi

# ---------------------------------------------------------------------------
# 4. cargo ISO: packages + guest payload

CARGO_STAGING="$WORKDIR/cargo"
mkdir -p "$CARGO_STAGING/packages" "$CARGO_STAGING/guest"
shopt -s nullglob
pkgs=("$PACKAGE_DIR"/$PKG_GLOB)
shopt -u nullglob
((${#pkgs[@]})) || die "no $PKG_GLOB packages in $PACKAGE_DIR" 66
cp "${pkgs[@]}" "$CARGO_STAGING/packages/"
for p in "${pkgs[@]}"; do basename "$p"; done > "$CARGO_STAGING/manifest.txt"
cp "$script_dir/guest/verify-session.py" "$CARGO_STAGING/guest/"
CARGO_ISO="$WORKDIR/cargo.iso"
cargo_iso "$CARGO_ISO" "$CARGO_STAGING"

# ---------------------------------------------------------------------------
# 5. run boot: fresh overlay over the golden image + cargo ISO

rm -f "$SERIAL_LOG"
RUN_DISK="$WORKDIR/run.qcow2"
qemu-img create -f qcow2 -b "$GOLDEN" -F qcow2 "$RUN_DISK" >/dev/null

timer_start
boot_vm "$RUN_DISK" "$CARGO_ISO"

if ((interactive)); then
    cat <<EOF

=================== interactive Krema QA VM ($TARGET_ID) ===================
  VNC:      vnc://127.0.0.1:$VNC_PORT   (display :$VNC_DISPLAY)
  SSH:      ssh -i $SSH_KEY -p $SSH_PORT -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null qa@127.0.0.1
  Serial:   $SERIAL_LOG
  Packages: ${pkgs[*]##*/}
  Session:  autologin 'qa' into Plasma (Wayland); krema autostarts.
Press Ctrl-C to shut the VM down (a screendump lands in $ARTIFACTS).
===========================================================================
EOF
    wait_for_ssh "$SSH_PORT" "$SSH_KEY" "$BOOT_TIMEOUT" \
        || warn "SSH is not up yet — the session may still be booting"
    trap 'log "shutting down"; screendump; ssh_qa "$SSH_PORT" "$SSH_KEY" "sudo -n poweroff" 2>/dev/null || true; wait_vm_exit 60 || true; QEMU_PID=""; exit 0' INT TERM
    while pid_alive "$QEMU_PID"; do sleep 2; done
    QEMU_PID=""
    log "VM exited"
    exit 0
fi

if ! wait_for_ssh "$SSH_PORT" "$SSH_KEY" "$BOOT_TIMEOUT"; then
    dump_guest_journal; screendump
    die "run boot: SSH never came up within ${BOOT_TIMEOUT}s (see $SERIAL_LOG)"
fi
record_time run_boot_ssh "$(timer_stop)"

# wait for the cargo install + sddm ordering to finish
timer_start
cargo_deadline=$((SECONDS + BOOT_TIMEOUT))
cargo_done=0
while ((SECONDS < cargo_deadline)); do
    if ssh_qa "$SSH_PORT" "$SSH_KEY" \
        'test -f /var/lib/krema-cargo-installed' 2>/dev/null; then
        cargo_done=1; break
    fi
    if ssh_qa "$SSH_PORT" "$SSH_KEY" \
        'systemctl is-failed -q krema-cargo-install.service' 2>/dev/null; then
        break
    fi
    sleep 5
done
if ((cargo_done == 0)); then
    dump_guest_journal; screendump
    die "krema-cargo-install.service failed or timed out (see $ARTIFACTS/boot-services.log)"
fi
record_time cargo_install "$(timer_stop)"

timer_start
set +e
ssh_qa "$SSH_PORT" "$SSH_KEY" \
    "python3 /run/krema-cargo/guest/verify-session.py \
        --report /tmp/krema-verify.json \
        --launch-app '$QA_APP' \
        --app-desktop '$QA_APP_DESKTOP' \
        --app-name-regex '$QA_APP_REGEX' \
        --item-timeout '$VERIFY_TIMEOUT'" \
    > "$ARTIFACTS/verify.log" 2>&1
verify_rc=$?
set -e
record_time verify "$(timer_stop)"
cat "$ARTIFACTS/verify.log" || true

scp_from_guest "$SSH_PORT" "$SSH_KEY" /tmp/krema-verify.json \
    "$ARTIFACTS/verify.json" || true

dump_guest_journal
screendump
ssh_qa "$SSH_PORT" "$SSH_KEY" 'sudo -n poweroff' 2>/dev/null || true
wait_vm_exit 120 || true
QEMU_PID=""

# ---------------------------------------------------------------------------
# 6. junit + summary

python3 - "$ARTIFACTS" <<'PYEOF' || true
import html, json, sys, time
from pathlib import Path

art = Path(sys.argv[1])
try:
    checks = json.loads((art / "verify.json").read_text())["checks"]
except Exception:
    checks = [{"name": "verifier.crashed", "ok": False, "seconds": 0,
               "detail": "verify.json missing; see verify.log"}]
cases = []
for c in checks:
    cls, _, _name = c["name"].partition(".")
    body = "" if c["ok"] else (
        f'<failure message="check failed">'
        f'{html.escape(str(c["detail"]))}</failure>')
    cases.append(
        f'  <testcase classname="krema.vmqa.{html.escape(cls)}" '
        f'name="{html.escape(c["name"])}" time="{c["seconds"]}">{body}'
        f'<system-out>{html.escape(str(c["detail"]))}</system-out></testcase>')
xml = (f'<testsuite name="krema-vm-qa" tests="{len(checks)}" '
       f'failures="{sum(1 for c in checks if not c["ok"])}" '
       f'timestamp="{time.strftime("%Y-%m-%dT%H:%M:%S")}">\n'
       + "\n".join(cases) + "\n</testsuite>\n")
(art / "junit.xml").write_text(xml)
PYEOF

{
    echo "target=$TARGET_ID"
    echo "image=$IMAGE_URL"
    echo "packages=${pkgs[*]##*/}"
    for kv in "${TIMINGS[@]}"; do echo "time_${kv%%=*}=${kv##*=}s"; done
    echo "verify_exit=$verify_rc"
} > "$ARTIFACTS/summary.txt"

if ((verify_rc != 0)); then
    die "guest verification failed ($verify_rc) — artifacts in $ARTIFACTS"
fi
log "PASS: $TARGET_ID (${pkgs[*]##*/}) — artifacts in $ARTIFACTS"
