# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
# Shared helpers for the Tier 3 VM QA harness. Sourced by run-vm-qa.sh and
# distro plug-ins; not meant to be executed directly.

# Kept POSIX-safe where practical, but callers run under bash.

KREMA_VM_VERSION=1

log() { printf '[run-vm-qa] %s\n' "$*" >&2; }
warn() { printf '[run-vm-qa] WARN: %s\n' "$*" >&2; }
die() { printf '[run-vm-qa] ERROR: %s\n' "$*" >&2; exit "${2:-1}"; }

# Elapsed-seconds timer helpers for the run summary.
timer_start() { _timer_t0=$SECONDS; }
timer_stop() { echo $((SECONDS - _timer_t0)); }

# --- tool requirements -----------------------------------------------------

have() { command -v "$1" >/dev/null 2>&1; }

# host_qemu_binary: prints the qemu system binary for the host arch.

# krema_arch / krema_arch_deb: host arch in distro image filename conventions.
krema_arch() {
    case "$(uname -m)" in
        x86_64|amd64) echo x86_64 ;;
        aarch64|arm64) echo aarch64 ;;
        *) uname -m ;;
    esac
}
krema_arch_deb() {
    case "$(uname -m)" in
        x86_64|amd64) echo amd64 ;;
        aarch64|arm64) echo arm64 ;;
        *) uname -m ;;
    esac
}
host_qemu_binary() {
    case "$(uname -m)" in
        x86_64|amd64) echo qemu-system-x86_64 ;;
        aarch64|arm64) echo qemu-system-aarch64 ;;
        *) return 1 ;;
    esac
}

# host_serial_console: kernel console device for the primary serial port.
host_serial_console() {
    case "$(uname -m)" in
        x86_64|amd64) echo ttyS0 ;;
        aarch64|arm64) echo ttyAMA0 ;;
        *) echo ttyS0 ;;
    esac
}

# require_tools: fail early with a distro-agnostic install hint.
require_tools() {
    local missing=() t qemu_bin os
    os="$(uname -s)"
    qemu_bin="$(host_qemu_binary)" || die "unsupported host: $os $(uname -m)"
    for t in "$qemu_bin" qemu-img curl ssh ssh-keygen scp python3; do
        have "$t" || missing+=("$t")
    done
    if ! have genisoimage && ! have mkisofs && ! have xorriso && ! have cloud-localds; then
        missing+=("genisoimage (or xorriso/mkisofs/cloud-localds)")
    fi
    if ((${#missing[@]})); then
        local hint
        case "$os" in
            Darwin)
                hint='  macOS (no Homebrew): nix shell nixpkgs#qemu nixpkgs#xorriso -c ./tests/vm/run-vm-qa.sh ...' ;;
            *)
                hint='  Debian/Ubuntu: sudo apt-get install qemu-system qemu-utils cloud-image-utils curl openssh-client python3
  Fedora:        sudo dnf install qemu-system-x86 qemu-img genisoimage curl openssh-clients python3
  (on aarch64 Linux hosts install qemu-system-arm and qemu-efi-aarch64 instead of qemu-system-x86/ovmf)' ;;
        esac
        die "missing host tools: ${missing[*]}
$hint"
    fi
    if [[ $os == Linux && ! -e /dev/kvm ]]; then
        warn "/dev/kvm is missing — the VM will run under TCG emulation (very slow)"
    fi
}

# pid_alive <pid>: portable liveness check (no /proc on macOS).
pid_alive() { kill -0 "$1" 2>/dev/null; }

# version_sort: natural version sort (BSD sort has no -V). stdin→stdout.
version_sort() {
    python3 -c 'import re,sys
lines=[l.rstrip("\n") for l in sys.stdin]
key=lambda s:[int(t) if t.isdigit() else t for t in re.split(r"([0-9]+)",s)]
print("\n".join(sorted(lines,key=key)))'
}

# --- filesystem helpers ------------------------------------------------------

# sha256_of <file>: portable digest.
sha256_of() {
    if have sha256sum; then sha256sum "$1" | awk '{print $1}';
    elif have shasum; then shasum -a 256 "$1" | awk '{print $1}';
    else python3 -c 'import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"; fi
}

# sha512_of <file>: portable digest.
sha512_of() {
    if have sha512sum; then sha512sum "$1" | awk '{print $1}';
    elif have shasum; then shasum -a 512 "$1" | awk '{print $1}';
    else python3 -c 'import hashlib,sys;print(hashlib.sha512(open(sys.argv[1],"rb").read()).hexdigest())' "$1"; fi
}

# image_digest_of <expected> <file>: digest of <file> in the notation of
# <expected> — a bare 64-hex sha256, or 'sha512:<hex>' (distros such as Debian
# publish only SHA512SUMS). Compare the output with <expected>.
image_digest_of() {
    case "$1" in
        sha512:*) echo "sha512:$(sha512_of "$2")" ;;
        *) sha256_of "$2" ;;
    esac
}

# make_iso <output.iso> <volid> <dir>: create a Rock Ridge/Joliet ISO.
make_iso() {
    local out="$1" volid="$2" dir="$3"
    if have genisoimage; then
        genisoimage -quiet -output "$out" -volid "$volid" -joliet -rock "$dir"
    elif have mkisofs; then
        mkisofs -quiet -o "$out" -V "$volid" -J -R "$dir"
    elif have xorriso; then
        xorriso -as mkisofs -quiet -o "$out" -V "$volid" -J -R "$dir"
    else
        die "no ISO creation tool (need genisoimage, mkisofs or xorriso)"
    fi
}

# download <url> <dest>: resumable download into dest (atomically moved).
download() {
    local url="$1" dest="$2"
    mkdir -p "$(dirname "$dest")"
    log "downloading $url"
    curl -fL --retry 3 --connect-timeout 30 -C - -o "$dest.part" "$url" \
        || die "download failed: $url"
    mv "$dest.part" "$dest"
}

# fetch <url>: fetch a small text file to stdout.
fetch() { curl -fsSL --retry 3 --connect-timeout 30 "$1"; }

# free_port [min]: an unused TCP port on 127.0.0.1, >= min (and >=1024).
free_port() {
    python3 - "${1:-0}" <<'EOF'
import socket, sys
minp = max(int(sys.argv[1] or 0), 1024)
for _ in range(200):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    if p >= minp:
        print(p)
        break
EOF
}

# free_vnc_display: a display number whose 5900+N port is free.
free_vnc_display() {
    python3 - <<'EOF'
import socket
for d in range(10, 100):
    s = socket.socket()
    try:
        s.bind(("127.0.0.1", 5900 + d))
    except OSError:
        s.close()
        continue
    s.close()
    print(d)
    break
EOF
}

# --- qemu helpers ------------------------------------------------------------

# host_uefi_args: prints the -drive pflash arguments for the host arch.
# The caller must create the vars copy beforehand: host_uefi_vars <dest>.
UEFI_CODE=""
UEFI_VARS=""

host_uefi_detect() {
    local os arch qemu_bin qemu_share code vars
    os="$(uname -s)"; arch="$(uname -m)"
    qemu_bin="$(command -v "$(host_qemu_binary)" 2>/dev/null || true)"
    # edk2 firmware shipped next to the qemu binary (nix store, Homebrew,
    # distro packages) — look relative to the binary first.
    for qemu_share in \
        "${qemu_bin%/bin/*}/share/qemu" \
        "${qemu_bin%/bin/*}/share" ; do
        [[ -d $qemu_share ]] || continue
        case "$arch" in
            aarch64|arm64)
                [[ -f $qemu_share/edk2-aarch64-code.fd ]] && UEFI_CODE="$qemu_share/edk2-aarch64-code.fd"
                ;;
            x86_64|amd64)
                [[ -f $qemu_share/edk2-x86_64-code.fd ]] && UEFI_CODE="$qemu_share/edk2-x86_64-code.fd"
                ;;
        esac
        [[ -n $UEFI_CODE ]] && break
    done

    if [[ $os == Linux ]]; then
        case "$arch" in
            aarch64|arm64)
                for code in /usr/share/AAVMF/AAVMF_CODE.fd /usr/share/qemu-efi-aarch64/QEMU_EFI.fd; do
                    [[ -f $code ]] && { UEFI_CODE="${UEFI_CODE:-$code}"; break; }
                done
                for vars in /usr/share/AAVMF/AAVMF_VARS.fd /usr/share/qemu-efi-aarch64/QEMU_VARS.fd; do
                    [[ -f $vars ]] && { UEFI_VARS="$vars"; break; }
                done
                ;;
            x86_64|amd64)
                for code in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/edk2/ovmf/OVMF_CODE.fd; do
                    [[ -f $code ]] && { UEFI_CODE="${UEFI_CODE:-$code}"; break; }
                done
                for vars in /usr/share/OVMF/OVMF_VARS_4M.fd /usr/share/OVMF/OVMF_VARS.fd /usr/share/edk2/ovmf/OVMF_VARS.fd; do
                    [[ -f $vars ]] && { UEFI_VARS="$vars"; break; }
                done
                ;;
        esac
    fi
    # nixpkgs/homebrew qemu ships no aarch64 vars template; run-vm-qa creates
    # a blank 64MiB 0xFF file (erased NOR flash) which EDK2 formats itself.
    UEFI_VARS_NEEDS_BLANK=""
    [[ -z $UEFI_VARS ]] && UEFI_VARS_NEEDS_BLANK=1
    [[ -n $UEFI_CODE ]] \
        || die "UEFI firmware not found for $arch on $os (install qemu-efi-aarch64 on arm64 Linux, ovmf on x86_64 Linux; on macOS use nixpkgs#qemu which ships edk2-*-code.fd)"
}

# prepare_efi_vars <dest>: writable VARS flash for this run.
prepare_efi_vars() {
    local dest="$1"
    if [[ -n $UEFI_VARS ]]; then
        cp "$UEFI_VARS" "$dest"
    else
        python3 - "$dest" <<'PYEOF'
import sys
open(sys.argv[1], "wb").write(b"\xff" * 64 * 1024 * 1024)
PYEOF
    fi
    chmod +w "$dest" 2>/dev/null || true
}

# --- ssh helpers -------------------------------------------------------------

# ssh_qa <port> <key> <command...>: run a command as qa inside the guest.
ssh_qa() {
    local port="$1" key="$2"; shift 2
    ssh -i "$key" \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o LogLevel=ERROR -o ConnectTimeout=10 \
        -o PreferredAuthentications=publickey -o BatchMode=yes \
        -p "$port" qa@127.0.0.1 "$@"
}

# scp_from_guest <port> <key> <remote> <local>
scp_from_guest() {
    local port="$1" key="$2" remote="$3" localf="$4"
    scp -i "$key" \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o LogLevel=ERROR -o ConnectTimeout=10 -o BatchMode=yes \
        -P "$port" "qa@127.0.0.1:$remote" "$localf"
}

# wait_for_ssh <port> <key> <timeout-seconds>: poll until ssh works.
wait_for_ssh() {
    local port="$1" key="$2" timeout="$3" deadline=$((SECONDS + $3))
    while ((SECONDS < deadline)); do
        if ssh_qa "$port" "$key" true 2>/dev/null; then
            return 0
        fi
        sleep 3
    done
    return 1
}

# --- QMP ----------------------------------------------------------------------

# qmp <socket> <json-command>: run one QMP command, print the response.
qmp() {
    python3 "$(dirname "${BASH_SOURCE[0]}")/qmp.py" "$1" "$2"
}

# qmp_screendump <socket> <out-path>: save a screenshot (png when supported).
qmp_screendump() {
    local sock="$1" out="$2" resp
    resp="$(qmp "$sock" "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"$out\",\"format\":\"png\"}}" 2>/dev/null || true)"
    if [[ $resp == *'"return"'* ]]; then
        return 0
    fi
    # Older QEMU: PPM only.
    resp="$(qmp "$sock" "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"$out\"}}" 2>/dev/null || true)"
    [[ $resp == *'"return"'* ]]
}
