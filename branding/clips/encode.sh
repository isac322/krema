#!/usr/bin/env bash
# Host side: trim and encode the raw recordings from clips.sh.
#   usage: branding/clips/encode.sh RAW_DIR [CLIP...]     (default: every clip below)
# Needs an ffmpeg with libvpx-vp9, libx264 and libwebp, e.g. run it through
#   nix shell nixpkgs#ffmpeg -c branding/clips/encode.sh /tmp/krema-clips-out/raw
# Writes website/media/<clip>.webm (VP9), <clip>.mp4 (H.264 High, yuv420p,
# faststart) and <clip>.webp (poster = first frame of the trimmed clip), all at
# 60 fps constant frame rate and the same size.
set -euo pipefail
RAW=${1:?raw dir}
shift
cd "$(dirname "$0")/../.."
OUT=website/media
mkdir -p "$OUT"

LIMIT=$((1800 * 1024))    # per-file budget in bytes
# Output widths to try, largest first (raw is 2560x1280 = 2x of 1280x640).
WIDTHS=(2560 1920 1600)
# Low CRFs first: the Roast Contours wallpaper is thin caramel lines on a
# near-black ground, which bands and shimmers at high CRF. VP9_CRFS and
# X264_CRFS override the ladders (e.g. to skip rungs a fast probe ruled out).
read -ra VP9_CRF <<<"${VP9_CRFS:-22 25 28 31 34 37}"
read -ra X264_CRF <<<"${X264_CRFS:-16 18 20 22 24 26 28}"

# clip  start(s)  duration(s): the recording minus its start-up and stop
# margins, so every clip starts and ends in the same resting state (seamless
# loop). Values for the GPU-session raws (rock5bp, real time): about 5.5 s of
# each raw is the recorder starting (ffmpeg probes its input before writing),
# then 0.5 s of rest before the first motion. Found from frame differences
# (first/last motion, 0.5 s before, 0.8 s after; previews and groups from the
# pointer's resting area, their windows never stop changing); re-check after
# regenerating. middle and pin are Xvfb-session clips.
declare -A START DUR
START[zoom]=5.65;      DUR[zoom]=8.02
START[launch]=5.57;    DUR[launch]=7.49
START[attention]=6.21; DUR[attention]=6.68
START[wheel]=5.68;     DUR[wheel]=7.37
START[middle]=0.4;     DUR[middle]=8.9
START[reorder]=5.63;   DUR[reorder]=8.88
START[pin]=0.4;        DUR[pin]=8.0
START[settings]=5.51;  DUR[settings]=9.21
START[styles]=5.58;    DUR[styles]=17.07
START[autohide]=5.82;  DUR[autohide]=5.75
START[dodge]=5.72;     DUR[dodge]=8.09
START[keyboard]=5.78;  DUR[keyboard]=6.13
START[previews]=5.56;  DUR[previews]=8.74
START[groups]=5.7;     DUR[groups]=9.4
START[progress]=5.74;  DUR[progress]=5.62

color=(-colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv)
size() { stat -f %z "$1" 2>/dev/null || stat -c %s "$1"; }

# try_fit OUTFILE CRF... -- ENCODER-ARGS: lowest CRF whose file fits LIMIT; returns 1 if none fits.
try_fit() {
    local out=$1 crf; shift
    local crfs=() ; while [ "$1" != -- ]; do crfs+=("$1"); shift; done; shift
    for crf in "${crfs[@]}"; do
        ffmpeg "${in[@]}" -vf "$filter" -an "${@/CRF/$crf}" "${color[@]}" "$out"
        if [ "$(size "$out")" -le "$LIMIT" ]; then echo "$crf"; return 0; fi
    done
    return 1
}

encode() {
    local clip=$1 src="$RAW/$1.mkv" w h vcrf xcrf
    in=(-hide_banner -loglevel error -y -ss "${START[$clip]}" -t "${DUR[$clip]}" -i "$src")
    for w in "${WIDTHS[@]}"; do
        h=$((w / 2))
        filter="fps=60,scale=$w:$h:flags=lanczos:out_color_matrix=bt709:out_range=tv,format=yuv420p"
        vcrf=$(try_fit "$OUT/$clip.webm" "${VP9_CRF[@]}" -- -c:v libvpx-vp9 -b:v 0 -crf CRF -row-mt 1 \
            -tile-columns 2 -deadline good -cpu-used 2 -g 300) || continue
        xcrf=$(try_fit "$OUT/$clip.mp4" "${X264_CRF[@]}" -- -c:v libx264 -profile:v high -preset veryslow \
            -crf CRF -g 300 -movflags +faststart) || continue
        break
    done
    ffmpeg -hide_banner -loglevel error -y -ss "${START[$clip]}" -i "$src" -frames:v 1 \
        -vf "scale=$w:$h:flags=lanczos" -c:v libwebp -quality 80 -compression_level 6 "$OUT/$clip.webp"
    printf '%-10s %4dx%-4d  webm crf %s %5d KB  mp4 crf %s %5d KB  webp %4d KB\n' "$clip" "$w" "$h" \
        "$vcrf" $(( $(size "$OUT/$clip.webm") / 1024 )) "$xcrf" $(( $(size "$OUT/$clip.mp4") / 1024 )) \
        $(( $(size "$OUT/$clip.webp") / 1024 ))
}

clips=("$@")
[ ${#clips[@]} -gt 0 ] || clips=(zoom launch attention wheel middle reorder pin settings styles autohide dodge keyboard previews groups progress)
for c in "${clips[@]}"; do
    [ -f "$RAW/$c.mkv" ] || { echo "skip $c (no $RAW/$c.mkv)"; continue; }
    encode "$c"
done
