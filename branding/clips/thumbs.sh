#!/usr/bin/env bash
# Host side of the GPU session (session-gpu.sh) on an RK3588 board with the
# Rockchip vendor kernel (Mali G610 on the kbase driver, VPU on
# /dev/mpp_service). Run it on that host from a scratch dir holding
#   src/        git archive of the Krema tree (CMakeLists.txt src packaging)
#   clips/      this directory
#   wallpaper/  branding/wallpaper (optional; clips.sh setup uses
#               roast-contours.png from it)
# usage: clips/thumbs.sh prepare   libmali + jellyfin-ffmpeg into hw/
#        clips/thumbs.sh build     image krema-thumbs (Dockerfile target gpu)
#        clips/thumbs.sh up        start the session container, wait for READY
#        clips/thumbs.sh run ARGS  clips.sh ARGS inside it; raws land in out/raw
#        clips/thumbs.sh down      remove the container
#        clips/thumbs.sh clean     down + remove the image and hw/ out/
# Needs docker (sudo -n is used when the user is not in the docker group).
set -euo pipefail
WORK=$(cd "$(dirname "$0")/.." && pwd)
NAME=krema-thumbs
DOCKER=(docker)
docker info >/dev/null 2>&1 || DOCKER=(sudo -n docker)

# libmali for the G610 (proprietary Arm userspace, local use only). g24p0
# matches the host's kbase g25p0 (UK 1.31); the wayland-gbm flavour gives
# KWin GBM + EGL. Jellyfin's image ships the same g24p0, as the gbm flavour.
MALI_URL=https://github.com/JeffyCN/mirrors/raw/libmali/lib/aarch64-linux-gnu/libmali-valhall-g610-g24p0-wayland-gbm.so
# jellyfin-ffmpeg (h264_rkmpp) comes from the Jellyfin image that the node
# already runs; the digest is the one isac322/homelab pins
# (values/jellyfin/backbone.yaml). It needs Debian's newer glibc, so its whole
# root filesystem is kept and ffmpeg runs through that root's own loader.
JF_IMAGE=${JF_IMAGE:-docker.io/jellyfin/jellyfin@sha256:78d3ea1207d1322471fcac39a614f004f2ccf7e878f95ab2977d752f07e4dd7e}

prepare() {
    mkdir -p "$WORK/hw/mali" "$WORK/out/raw"
    chmod 777 "$WORK/out" "$WORK/out/raw"
    if [ ! -f "$WORK/hw/mali/libmali.so" ]; then
        curl -fsSL -o "$WORK/hw/mali/libmali.so" "$MALI_URL"
    fi
    local l
    for l in libmali.so.1 libEGL.so.1 libGLESv2.so.2 libGLESv1_CM.so.1 libgbm.so.1; do
        ln -sf libmali.so "$WORK/hw/mali/$l"
    done
    [ -x "$WORK/hw/jf/usr/lib/jellyfin-ffmpeg/ffmpeg" ] && return
    local tar=$WORK/hw/jellyfin.tar tmp=$WORK/hw/jellyfin-oci
    # Export from the node's containerd (Kubernetes namespace) when the image
    # is there, else pull it with docker.
    if sudo -n ctr -n k8s.io images ls -q 2>/dev/null | grep -qx "$JF_IMAGE"; then
        sudo -n ctr -n k8s.io images export --platform linux/arm64 "$tar" "$JF_IMAGE"
    else
        "${DOCKER[@]}" pull --platform linux/arm64 "$JF_IMAGE"
        "${DOCKER[@]}" save -o "$tar" "$JF_IMAGE"
        "${DOCKER[@]}" rmi "$JF_IMAGE" >/dev/null
    fi
    sudo -n chown "$(id -u):$(id -g)" "$tar" 2>/dev/null || true
    rm -rf "$tmp" "$WORK/hw/jf"
    mkdir -p "$tmp" "$WORK/hw/jf"
    tar xf "$tar" -C "$tmp"
    for l in $(python3 -c 'import json, sys; print(" ".join(json.load(open(sys.argv[1]))[0]["Layers"]))' "$tmp/manifest.json"); do
        tar xzf "$tmp/$l" -C "$WORK/hw/jf" --exclude='dev/*' 2>/dev/null || tar xf "$tmp/$l" -C "$WORK/hw/jf" --exclude='dev/*'
    done
    rm -rf "$tmp" "$tar"
}

build() {
    "${DOCKER[@]}" build -q --build-context krema-src="$WORK/src" --target gpu -t "$NAME" "$WORK/clips"
}

up() {
    local dev args=() g gids=""
    # The devices Jellyfin gets for RKMPP, plus the GPU. /dev/dma_heap is a
    # directory, so its nodes are passed one by one.
    for dev in /dev/dri/* /dev/mali0 /dev/mpp_service /dev/rga /dev/dma_heap/*; do
        [ -c "$dev" ] || continue
        args+=(--device "$dev")
        g=$(stat -c %g "$dev")
        [ "$g" = 0 ] || case " $gids " in *" $g "*) ;; *) gids+=" $g" ;; esac
    done
    for g in $gids; do args+=(--group-add "$g"); done
    [ -d "$WORK/wallpaper" ] && args+=(-v "$WORK/wallpaper:/wallpaper:ro")
    "${DOCKER[@]}" rm -f "$NAME" >/dev/null 2>&1 || true
    # CPUS (default 4) caps the container; --cpu-shares 256 lets the host's
    # other workloads win whenever they need the CPU.
    # --ulimit core=0: kded6 (D-Bus activated) crashes at start in this
    # headless session and would leave a `core` file in the home folder the
    # Dolphin shots show.
    "${DOCKER[@]}" run -d --name "$NAME" --cpus "${CPUS:-4}" --cpu-shares 256 --memory 6g --shm-size 1g \
        --ulimit core=0 "${args[@]}" \
        -e SPEED="${SPEED:-1}" -e MALI="${MALI:-1}" -e W="${W:-1280}" -e H="${H:-720}" -e SCALE="${SCALE:-2}" \
        -e LP_NUM_THREADS="${LP_NUM_THREADS:-2}" \
        -v "$WORK/clips:/clips:ro" -v "$WORK/out:/out" \
        -v "$WORK/hw/mali:/opt/mali:ro" -v "$WORK/hw/jf:/opt/jf:ro" \
        "$NAME" dbus-run-session bash /clips/session-gpu.sh >/dev/null
    for _ in $(seq 120); do
        "${DOCKER[@]}" logs "$NAME" 2>&1 | grep -q '^READY' && return 0
        sleep 1
    done
    echo "session did not start" >&2
    "${DOCKER[@]}" logs "$NAME" >&2
    return 1
}

run() { "${DOCKER[@]}" exec "$NAME" bash /clips/clips.sh "$@"; }
down() { "${DOCKER[@]}" rm -f "$NAME" >/dev/null 2>&1 || true; }
clean() {
    down
    "${DOCKER[@]}" rmi "$NAME" >/dev/null 2>&1 || true
    rm -rf "$WORK/hw" "$WORK/out"
}

cmd=${1:?usage: $0 prepare|build|up|run ARGS|down|clean}
shift
"$cmd" "$@"
