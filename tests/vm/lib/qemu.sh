# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
# QEMU launch helper for run-vm-qa.sh. Expects common.sh already sourced and
# host_uefi_detect() to have populated UEFI_CODE/UEFI_VARS(_NEEDS_BLANK).

# vm_qemu_args <ssh_port> <vnc_display> <disk> <seed> <cargo> <serial_log> \
#              <qmp_sock> <pidfile> <mem_mb> <smp> <heads> <efi_vars>
# Fills the global array QEMU_ARGS (works on bash 3.2, no mapfile).
vm_qemu_args() {
    local ssh_port="$1" vnc_display="$2" disk="$3" seed="$4" cargo="$5"
    local serial_log="$6" qmp_sock="$7" pidfile="$8" mem_mb="$9" smp="${10}"
    local heads="${11}" efi_vars="${12}"
    local os arch
    os="$(uname -s)"; arch="$(uname -m)"

    QEMU_ARGS=()
    if [[ $os == Darwin ]]; then
        # Native Hypervisor.framework — aarch64 guests only.
        QEMU_ARGS+=(qemu-system-aarch64
                    -machine virt,highmem=on -accel hvf -cpu host)
    else
        case "$arch" in
            x86_64|amd64)
                QEMU_ARGS+=(qemu-system-x86_64
                            -machine q35,accel=kvm -cpu host)
                ;;
            aarch64|arm64)
                QEMU_ARGS+=(qemu-system-aarch64
                            -machine virt,accel=kvm,gic-version=host -cpu host)
                ;;
        esac
        [[ -e /dev/kvm ]] || warn "KVM unavailable; booting with TCG emulation"
    fi

    # UEFI pflash: read-only code + per-run writable vars copy.
    QEMU_ARGS+=(
        -drive "if=pflash,format=raw,readonly=on,file=$UEFI_CODE"
        -drive "if=pflash,format=raw,file=$efi_vars"
    )

    # virtio-blk for everything: the seed ISO and cargo ISO appear as
    # /dev/vd{b,c} with an iso9660 filesystem (cloud-init finds the 'cidata'
    # label; cargo-install mounts 'KREMACARGO').
    QEMU_ARGS+=(
        -m "$mem_mb" -smp "$smp"
        -drive "file=$disk,if=virtio,format=qcow2"
        -drive "file=$seed,if=virtio,format=raw,readonly=on"
    )
    if [[ -n $cargo ]]; then
        QEMU_ARGS+=(-drive "file=$cargo,if=virtio,format=raw,readonly=on")
    fi

    QEMU_ARGS+=(
        -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:${ssh_port}-:22"
        -device virtio-net-pci,netdev=net0
        -vga none
        -device "virtio-gpu-pci,max_outputs=${heads}"
        -device virtio-keyboard-pci
        -device virtio-tablet-pci
        -device virtio-rng-pci
        -display none
        -vnc "127.0.0.1:${vnc_display}"
        -serial "file:$serial_log"
        -qmp "unix:$qmp_sock,server=on,wait=off"
        -pidfile "$pidfile"
        -daemonize
    )
}
