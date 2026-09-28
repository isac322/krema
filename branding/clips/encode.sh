#!/usr/bin/env bash
# Host side: trim and encode the raw recordings from clips.sh.
#   usage: branding/clips/encode.sh RAW_DIR [CLIP...]     (default: every clip in RAW_DIR)
# Needs an ffmpeg with libvpx-vp9, libx264 and libwebp, e.g. run it through
#   nix shell nixpkgs#ffmpeg -c branding/clips/encode.sh /tmp/krema-clips-out/raw
# Writes website/media/<clip>.webm (VP9), <clip>.mp4 (H.264 High, yuv420p,
# faststart) and <clip>.webp (poster = first frame of the trimmed clip), all
# 2560x1280 at 60 fps constant frame rate.
#
# Each video is a 2-pass encode aimed at the per-file budget (LIMIT) over the
# clip's length, so one encode lands just under it; no CRF ladder. VP9 runs
# in constrained quality (-crf 20 caps the quality, so a quiet clip comes out
# smaller than the budget). Every encode of every clip runs in parallel, JOBS
# at a time (default: one per two cores), each with THREADS threads.
set -euo pipefail
RAW=$(cd "${1:?raw dir}" && pwd)
shift
cd "$(dirname "$0")/../.."
OUT=$PWD/website/media
mkdir -p "$OUT"
export RAW OUT
export LIMIT=$((1800 * 1024))   # per-file budget in bytes
NCPU=$(sysctl -n hw.ncpu 2>/dev/null || nproc)
JOBS=${JOBS:-$(( (NCPU + 1) / 2 ))}
export THREADS=${THREADS:-4}

# Trim: clips.sh (GPU session) writes RAW/<clip>.trim, "START DUR" in seconds
# from the recorder's marks (record.py --trim): the recording minus its
# start-up and stop, so every clip starts and ends in the same resting state
# (seamless loop). The Xvfb-session clips have no marks; theirs were found
# from frame differences (first/last motion, 0.5 s before, 0.8 s after).
export TRIM_middle="0.4 8.9" TRIM_pin="0.4 8.0"

job() {   # job CLIP webm|mp4|webp
    local clip=$1 kind=$2 src="$RAW/$1.mkv" trim start dur kbps log
    trim=$(cat "$RAW/$clip.trim" 2>/dev/null || eval echo "\${TRIM_$clip:-}")
    [ -n "$trim" ] || { echo "$clip: no trim points" >&2; return 1; }
    read -r start dur <<<"$trim"
    local in=(-hide_banner -loglevel error -y -ss "$start" -t "$dur" -i "$src")
    local vf="fps=60,scale=2560:1280:flags=lanczos:out_color_matrix=bt709:out_range=tv,format=yuv420p"
    local color=(-colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv)
    # Budget minus 4 % for the container and the rate control's overshoot.
    kbps=$(awk -v b="$LIMIT" -v d="$dur" 'BEGIN { printf "%d", b * 8 * 0.96 / d / 1000 }')
    log=$(mktemp -u "${TMPDIR:-/tmp}/krema-$clip-$kind.XXXX")
    case $kind in
    webm)
        local vp9=(-c:v libvpx-vp9 -b:v "${kbps}k" -crf 20 -row-mt 1 -tile-columns 2 -threads "$THREADS"
                   -deadline good -g 300 -auto-alt-ref 1 -lag-in-frames 25 -passlogfile "$log")
        ffmpeg "${in[@]}" -vf "$vf" -an "${vp9[@]}" -cpu-used 4 -pass 1 -f null /dev/null
        ffmpeg "${in[@]}" -vf "$vf" -an "${vp9[@]}" -cpu-used 2 -pass 2 "${color[@]}" "$OUT/$clip.webm" ;;
    mp4)
        local x264=(-c:v libx264 -profile:v high -preset slow -b:v "${kbps}k" -g 300 -threads "$THREADS"
                    -passlogfile "$log")
        ffmpeg "${in[@]}" -vf "$vf" -an "${x264[@]}" -pass 1 -f null /dev/null
        ffmpeg "${in[@]}" -vf "$vf" -an "${x264[@]}" -pass 2 "${color[@]}" -movflags +faststart "$OUT/$clip.mp4" ;;
    webp)
        ffmpeg -hide_banner -loglevel error -y -ss "$start" -i "$src" -frames:v 1 \
            -vf "scale=2560:1280:flags=lanczos" -c:v libwebp -quality 80 -compression_level 6 "$OUT/$clip.webp" ;;
    esac
    rm -f "$log"*
}
export -f job

clips=("$@")
if [ ${#clips[@]} -eq 0 ]; then
    for f in "$RAW"/*.mkv; do clips+=("$(basename "$f" .mkv)"); done
fi
todo=()
for c in "${clips[@]}"; do
    [ -f "$RAW/$c.mkv" ] && todo+=("$c") || echo "skip $c (no $RAW/$c.mkv)" >&2
done
# Longest jobs first: VP9, then H.264, then the posters.
for kind in webm mp4 webp; do
    for c in "${todo[@]}"; do echo "$c $kind"; done
done | xargs -P "$JOBS" -L 1 bash -c 'job "$0" "$1"'

size() { stat -f %z "$1" 2>/dev/null || stat -c %s "$1"; }
status=0
for c in "${todo[@]}"; do
    printf '%-10s webm %5d KB  mp4 %5d KB  webp %4d KB\n' "$c" $(( $(size "$OUT/$c.webm") / 1024 )) \
        $(( $(size "$OUT/$c.mp4") / 1024 )) $(( $(size "$OUT/$c.webp") / 1024 ))
    for f in "$OUT/$c.webm" "$OUT/$c.mp4"; do
        [ "$(size "$f")" -le "$LIMIT" ] || { echo "over budget: $f" >&2; status=1; }
    done
done
exit $status
