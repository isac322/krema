#!/usr/bin/env bash
# Host side: crop, trim and encode the raw recordings from clips.sh.
#   usage: branding/clips/encode.sh RAW_DIR [CLIP...]     (default: every clip below)
# Needs an ffmpeg with libvpx-vp9, libx264 and libwebp, e.g. run it through
#   nix shell nixpkgs#ffmpeg -c branding/clips/encode.sh /tmp/krema-clips-out/raw
# Writes website/media/<clip>.webm (VP9), <clip>.mp4 (H.264 High, yuv420p,
# faststart) and <clip>.webp (poster = first frame of the trimmed clip).
set -euo pipefail
RAW=${1:?raw dir}
shift
cd "$(dirname "$0")/../.."
OUT=website/media
mkdir -p "$OUT"

CROP=1280:640:320:440     # w:h:x:y in the 1920x1080 recording
FPS=30                    # the compositor updates ~30 times a second in this setup
LIMIT=$((1200 * 1024))    # per-file budget in bytes

# clip  start(s)  duration(s): trim points picked from contact sheets so every
# clip starts and ends in the same resting state (seamless loop).
declare -A START DUR
START[zoom]=0.9;      DUR[zoom]=7.9
START[launch]=0.6;    DUR[launch]=7.9
START[attention]=0.6; DUR[attention]=6.9
START[settings]=1.2;  DUR[settings]=8.0
START[autohide]=1.6;  DUR[autohide]=6.4

filter="crop=$CROP,fps=$FPS,scale=out_color_matrix=bt709:out_range=tv,format=yuv420p"
color=(-colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv)

size() { stat -f %z "$1" 2>/dev/null || stat -c %s "$1"; }

encode() {
    local clip=$1 src="$RAW/$1.mkv" crf
    local in=(-hide_banner -loglevel error -y -ss "${START[$clip]}" -t "${DUR[$clip]}" -i "$src")
    for crf in 30 33 36 39 42 45; do
        ffmpeg "${in[@]}" -vf "$filter" -an -c:v libvpx-vp9 -b:v 0 -crf "$crf" -row-mt 1 \
            -deadline good -cpu-used 1 -g 240 "${color[@]}" "$OUT/$clip.webm"
        [ "$(size "$OUT/$clip.webm")" -le "$LIMIT" ] && break
    done
    echo "$clip.webm crf=$crf $(size "$OUT/$clip.webm")"
    for crf in 23 25 27 29 31 33; do
        ffmpeg "${in[@]}" -vf "$filter" -an -c:v libx264 -profile:v high -preset veryslow -crf "$crf" \
            -g 240 "${color[@]}" -movflags +faststart "$OUT/$clip.mp4"
        [ "$(size "$OUT/$clip.mp4")" -le "$LIMIT" ] && break
    done
    echo "$clip.mp4 crf=$crf $(size "$OUT/$clip.mp4")"
    ffmpeg -hide_banner -loglevel error -y -ss "${START[$clip]}" -i "$src" -frames:v 1 \
        -vf "crop=$CROP" -c:v libwebp -quality 82 -compression_level 6 "$OUT/$clip.webp"
    echo "$clip.webp $(size "$OUT/$clip.webp")"
}

clips=("$@")
[ ${#clips[@]} -gt 0 ] || clips=(zoom launch attention settings autohide)
for c in "${clips[@]}"; do encode "$c"; done
