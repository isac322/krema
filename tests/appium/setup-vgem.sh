#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Build and load a vgem kernel module out-of-tree for hosts whose kernel
# ships no vgem (e.g. GitHub ubuntu-latest runners' Azure kernels). A vgem
# device provides a DRM render node, which makes
# `kwin_wayland --virtual` composite with OpenGL (llvmpipe) instead of
# QPainter, and enables `--output-count` for KREMA_E2E_OUTPUT_COUNT>1.
#
#   sudo tests/appium/setup-vgem.sh
#
# Environment:
#   VGEM_KO   optional cache path for the built module (CI caches it keyed on
#             `uname -r` and this script's hash). When it exists it is loaded
#             directly: no kernel headers, fetch or build needed. A built
#             module is copied there after insmod.
#
# Needs: kernel headers for the running kernel (linux-headers-*), make, gcc,
# curl, and no Secure Boot lockdown (GitHub runners ship SecureBoot=disabled).
# Undo: rmmod vgem.
#
# Sources come from the stable tree matching the running kernel's major.minor
# (linux-X.Y.y); vgem's driver surface is small and stable, so the nearest
# stable branch works. Falls back to master.
#
# Since 6.15 vgem registers a faux device; it is built back as the platform
# device "vgem" it was before. KWin < 6.5 recognises vgem (and opens its
# primary node, the only one gbm can allocate dumb buffers on) only on the
# platform bus, and libdrm < 2.4.126 does not enumerate faux devices at all:
# with a faux vgem those KWin versions composite with QPainter or fail to
# allocate buffers on the render node.

set -eu

# A packaged vgem only helps if the kernel still registers it on the platform
# bus (before 6.15). Otherwise it is replaced by the out-of-tree build below.
vgem_on_faux() { [ -e /sys/bus/faux/devices/vgem ]; }
if [ -d /sys/module/vgem ] || lsmod | grep -q '^vgem'; then
    if vgem_on_faux; then
        echo "the loaded vgem is a faux device; unload it first (rmmod vgem, nothing may hold /dev/dri)" >&2
        exit 1
    fi
    echo "[setup-vgem] vgem already loaded"
    exit 0
fi
if modprobe vgem 2>/dev/null; then
    if ! vgem_on_faux; then
        echo "[setup-vgem] loaded packaged vgem"
        exit 0
    fi
    rmmod vgem
    echo "[setup-vgem] packaged vgem is a faux device: building the platform one"
fi

# Cache hit: load the module built for this kernel and this script (CI keys
# its actions/cache entry on uname -r + hashFiles(setup-vgem.sh)); an
# unloadable one falls through to the build below.
ko=${VGEM_KO:-}
if [ -n "$ko" ] && [ -f "$ko" ]; then
    if insmod "$ko" 2>/dev/null; then
        echo "[setup-vgem] loaded cached $ko"
        ls -l /dev/dri
        exit 0
    fi
    echo "[setup-vgem] cached $ko did not load; building" >&2
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

if grep -q 'faux_device_create' "$work/vgem_drv.c"; then
    sed -i \
        -e 's|#include <linux/device/faux.h>|#include <linux/platform_device.h>|' \
        -e 's|struct faux_device \*|struct platform_device *|g' \
        -e 's|faux_device_create("vgem", NULL, NULL)|platform_device_register_simple("vgem", -1, NULL, 0)|' \
        -e 's|if (!fdev)|if (IS_ERR_OR_NULL(fdev))|' \
        -e 's|faux_device_destroy(|platform_device_unregister(|g' \
        "$work/vgem_drv.c"
    echo "[setup-vgem] registering vgem as a platform device"
fi

printf 'obj-m := vgem.o\nvgem-y := vgem_drv.o vgem_fence.o\n' >"$work/Makefile"
make -C "$kdir" M="$work" modules
insmod "$work/vgem.ko"
if [ -n "$ko" ]; then
    mkdir -p "$(dirname "$ko")"
    cp "$work/vgem.ko" "$ko"
    # Hand dir and file back to the invoking user: the CI cache restore runs
    # unprivileged and must be able to read and replace them.
    [ -n "${SUDO_UID:-}" ] &&
        chown "$SUDO_UID:${SUDO_GID:-$SUDO_UID}" "$(dirname "$ko")" "$ko" 2>/dev/null || true
fi
ls -l /dev/dri
echo "[setup-vgem] loaded vgem (module in $work)"
