#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Create a vkms device with N connected connectors through the vkms configfs
# ABI (kernel >= 6.19) and print its /dev/dri/cardN node.
#
#   sudo tests/appium/setup-vkms.sh        # one device named "krema-e2e", 2 outputs
#   sudo tests/appium/setup-vkms.sh 3      # 3 outputs
#
# Needs a loaded vkms module, configfs, and root. The vkms configfs ABI is
# kernel >= 6.19 (upstream file drivers/gpu/drm/vkms/vkms_configfs.c); on
# older kernels /sys/kernel/config/vkms never appears — use
# tests/appium/setup-vgem.sh instead (a vgem render node enables
# kwin --virtual --output-count). The new card's sysfs device path ends in
# the configfs device name, so entrypoint.sh picks it up
# automatically for a KREMA_E2E_OUTPUT_COUNT=n DRM session.
#
# Undo: disable (echo 0 > enabled), remove the symlinks under
# possible_crtcs/possible_encoders, then rmdir the tree bottom-up — or
# `rmmod vkms`, which removes every vkms device including the default one.

set -eu

count=${1:-2}
name=${2:-krema-e2e}

config=/sys/kernel/config/vkms
if [ ! -d "$config" ]; then
    # configfs is a regular mount; on most systems /sys/kernel/config is
    # already mounted, on minimal hosts it is not.
    mount -t configfs none /sys/kernel/config 2>/dev/null || true
fi
if [ ! -d "$config" ]; then
    echo "$config missing (kernel $(uname -r))" >&2
    echo "vkms must be loaded and built with the configfs ABI (kernel >= 6.19)" >&2
    ls /sys/kernel/config/ 2>/dev/null >&2 || echo "/sys/kernel/config is not mounted" >&2
    exit 1
fi
# Idempotent: an existing device of this name is already what the caller
# wants (its connector count is verified below).
if [ ! -d "$config/$name" ]; then
    mkdir "$config/$name"
    for i in $(seq "$count"); do
        mkdir "$config/$name/planes/plane$i"
        mkdir "$config/$name/crtcs/crtc$i"
        mkdir "$config/$name/encoders/enc$i"
        mkdir "$config/$name/connectors/conn$i"
        # type 1 = primary plane; link plane and encoder to the crtc, the
        # connector to the encoder.
        echo 1 >"$config/$name/planes/plane$i/type"
        ln -s "$config/$name/crtcs/crtc$i" "$config/$name/planes/plane$i/possible_crtcs/crtc$i"
        ln -s "$config/$name/crtcs/crtc$i" "$config/$name/encoders/enc$i/possible_crtcs/crtc$i"
        ln -s "$config/$name/encoders/enc$i" "$config/$name/connectors/conn$i/possible_encoders/enc$i"
    done
    echo 1 >"$config/$name/enabled"
fi

card=
for c in /dev/dri/card*; do
    dev=$(readlink -f "/sys/class/drm/${c##*/}/device" 2>/dev/null) || continue
    case "$dev" in
    */"$name") card=$c ;;
    esac
done
[ -n "$card" ] || { echo "device enabled but no /dev/dri card found for $name" >&2; exit 1; }

connected=0
for status in "/sys/class/drm/${card##*/}"-*/status; do
    [ -f "$status" ] || continue
    [ "$(cat "$status")" = connected ] && connected=$((connected + 1))
done
echo "[setup-vkms] $card ($name): $connected connected output(s)"
[ "$connected" -eq "$count" ] || exit 1
