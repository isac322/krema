#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Build and load a vgem kernel module out-of-tree for hosts whose kernel
# ships no vgem (e.g. GitHub ubuntu-latest runners: Azure kernels package
# vkms but not vgem). A vgem device provides a DRM render node, which makes
# `kwin_wayland --virtual` composite with OpenGL (llvmpipe) instead of
# QPainter, and enables `--output-count` for KREMA_E2E_OUTPUT_COUNT>1.
#
#   sudo tests/appium/setup-vgem.sh
#
# Needs: kernel headers for the running kernel (linux-headers-*), make, gcc,
# curl, and no Secure Boot lockdown (GitHub runners ship SecureBoot=disabled).
# Undo: rmmod vgem.
#
# Sources come from the stable tree matching the running kernel's major.minor
# (linux-X.Y.y); vgem's driver surface is small and stable, so the nearest
# stable branch works. Falls back to master.

set -eu

if [ -d /sys/module/vgem ] || lsmod | grep -q '^vgem'; then
    echo "[setup-vgem] vgem already loaded"
    exit 0
fi
if modprobe vgem 2>/dev/null; then
    echo "[setup-vgem] loaded packaged vgem"
    exit 0
fi

kdir=/lib/modules/$(uname -r)/build
[ -d "$kdir" ] || {
    echo "no headers for $(uname -r) (expected $kdir); install linux-headers-$(uname -r)" >&2
    exit 1
}

mm=$(uname -r | cut -d. -f1-2)
work=$(mktemp -d)
fetch() {
    for base in \
        "https://gitlab.com/linux-kernel/stable/-/raw/linux-$mm.y" \
        "https://raw.githubusercontent.com/torvalds/linux/master"; do
        ok=1
        for f in vgem_drv.c vgem_drv.h vgem_fence.c; do
            curl -fsSL --max-time 30 "$base/drivers/gpu/drm/vgem/$f" -o "$work/$f" || { ok=; break; }
        done
        [ -n "$ok" ] && { echo "[setup-vgem] sources: $base"; return 0; }
    done
    return 1
}
fetch || { echo "could not fetch vgem sources" >&2; exit 1; }

printf 'obj-m := vgem.o\nvgem-y := vgem_drv.o vgem_fence.o\n' >"$work/Makefile"
make -C "$kdir" M="$work" modules
insmod "$work/vgem.ko"
ls -l /dev/dri
echo "[setup-vgem] loaded vgem (module in $work)"
