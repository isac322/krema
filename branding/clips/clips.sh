#!/bin/bash
# Runs inside the container after session.sh (or session-gpu.sh) prints READY.
#   usage: clips.sh setup            start plasmashell + Krema with the demo config
#          clips.sh CLIP...          record clips (see CLIPS below)
#          clips.sh all              setup + every clip of this session, in order
# Raw recordings (lossless, the bottom 1280x640 logical px of the screen =
# 2560x1280 physical px) go to /out/raw; encode.sh on the host trims and
# encodes them.
#
# Two sessions (see NOTES.md):
#   session.sh      KWin X11-windowed on Xvfb, QPainter compositing. Recorded
#                   with ffmpeg x11grab. No screencasts, so no window previews.
#   session-gpu.sh  KWin virtual backend, OpenGL on a DRM render node (GPU=1 in
#                   session.env). Recorded from a KWin screencast (sessiontool +
#                   GStreamer pipewiresrc). Needed for previews and groups.
# Both take input from xdotool (Xvfb, or Xwayland's XTEST -> libei -> KWin).
#
# Coordinates in this file are logical px (1280x720 screen); p() converts to
# the X11 pixels that xdotool uses.
set -eu
. /tmp/session.env
[ -n "${GPU:-}" ] || export DISPLAY=:99
cd "$HOME"
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RAW=/out/raw
mkdir -p "$RAW"
# X11 px per logical px for xdotool: SCALE on Xvfb. On Xwayland, KWin maps
# XTEST's absolute motion (via libei) onto logical px, whatever size X reports.
S=$SCALE
[ -z "${GPU:-}" ] || S=1
CROP_Y=$((H - 640))       # the recorded band: logical (0, CROP_Y) 1280x640

if [ -n "${GPU:-}" ]; then
    CLIPS="zoom attention wheel reorder dodge autohide keyboard styles settings launch previews groups progress"
else
    CLIPS="zoom launch attention wheel middle reorder pin settings styles autohide dodge keyboard"
fi
# Wallpaper: branding/wallpaper/roast-contours.png, mounted at /wallpaper by
# thumbs.sh; Plasma's "Next" where it is missing (Xvfb session).
WALLPAPER=file:///usr/share/wallpapers/Next/
[ -f /wallpaper/roast-contours.png ] && WALLPAPER=file:///wallpaper/roast-contours.png

# Dock geometry (IconSize=72, IconSpacing=4, launchers below): icon i
# (0-based) is centred at x = ICON_X0 + ICON_STEP*i, y = ICON_Y. Measured from
# a still of this session; `clips.sh geometry` prints a still to check it.
ICON_Y=${ICON_Y:-664}
ICON_X0=${ICON_X0:-374} ICON_STEP=76
LAUNCHERS=(org.kde.dolphin org.mozilla.firefox org.kde.konsole org.kde.kate org.kde.okular org.kde.gwenview org.kde.kcalc systemsettings)
DOLPHIN=0 FIREFOX=1 KONSOLE=2 KATE=3 KCALC=6 SYSSETTINGS=7
icon_x() { echo $((ICON_X0 + ICON_STEP * $1)); }

p() { echo $(($1 * S)); }
move() { xdotool mousemove "$(p "$1")" "$(p "$2")"; }

# Pointer glide in logical px: glide X1 Y1 X2 Y2 SECONDS. Smoothstep easing at
# 120 Hz. The GPU session (real time) uses glide.py, which paces every step on
# its own deadline. The Xvfb session runs one xdotool command chain, so the
# pacing comes from xdotool's sleep on the session's slowed clock; that chain
# runs about 5 % long (each call adds to the step time).
glide() {
    if [ -n "${GPU:-}" ]; then
        python3 "$HERE/glide.py" "$(p "$1")" "$(p "$2")" "$(p "$3")" "$(p "$4")" "$5" 2>/dev/null
        return
    fi
    xdotool $(awk -v a="$1" -v b="$2" -v c="$3" -v d="$4" -v t="$5" -v s="$S" 'BEGIN {
        n = int(t * 120 + 0.5); if (n < 1) n = 1
        for (i = 1; i <= n; i++) { u = i / n; u = u * u * (3 - 2 * u)
            printf "mousemove %d %d sleep 0.0083 ", (a + (c - a) * u) * s + 0.5, (b + (d - b) * u) * s + 0.5 } }')
}

# Run a KWin script given as a string.
kwin_js() {
    local f
    f=$(mktemp /tmp/kwinXXXX.js)
    printf '%s\n' "$1" >"$f"
    "$HERE/kwinscript.sh" "$f" >/dev/null
    rm -f "$f"
}
# place CLASS-SUBSTRING X Y W H (logical px, frame geometry); raise + activate.
place() {
    kwin_js "for (const w of workspace.windowList()) {
        if (!w.normalWindow || w.resourceClass.indexOf('$1') < 0) continue;
        w.minimized = false; w.noBorder = false;
        w.frameGeometry = {x: $2, y: $3, width: $4, height: $5};
        workspace.activeWindow = w; print('PLACED $1'); }"
}
close_class() {
    kwin_js "for (const w of workspace.windowList()) if (w.normalWindow && w.resourceClass.indexOf('$1') >= 0) w.closeWindow();"
}
count_class() {   # number of normal windows whose class contains $1
    local before
    before=$(grep -c "COUNT-$1" /tmp/kwin.log || true)
    kwin_js "let n = 0; for (const w of workspace.windowList()) if (w.normalWindow && w.resourceClass.indexOf('$1') >= 0) n++; print('COUNT-$1 ' + n);"
    grep "COUNT-$1" /tmp/kwin.log | tail -n 1 | awk '{print $NF}'
}
wait_class() {    # wait_class CLASS N: wait until N windows of CLASS exist
    local i
    for i in $(seq 300); do
        [ "$(count_class "$1")" -ge "$2" ] && return 0
        sleep 0.1
    done
    echo "timeout waiting for $1" >&2
}
wait_gone() {     # wait_gone CLASS: wait until no window of CLASS is left
    local i
    for i in $(seq 300); do
        [ "$(count_class "$1")" -eq 0 ] && return 0
        sleep 0.1
    done
    echo "timeout closing $1" >&2
}
# kwin_until JS MARK: run the KWin script JS again and again until it prints
# MARK (it prints it once its window is there and it has done its job).
kwin_until() {
    local i n
    n=$(grep -c "$2" /tmp/kwin.log || true)
    for i in $(seq 200); do
        kwin_js "$1"
        [ "$(grep -c "$2" /tmp/kwin.log)" -gt "$n" ] && return 0
        sleep 0.05
    done
    echo "timeout waiting for $2" >&2
}

# GPU session: a KWin screencast of the recorded band (the pointer embedded),
# read by record.py (GStreamer) and piped as I420 Matroska through a FIFO into
# jellyfin-ffmpeg, which encodes it on the RK3588 VPU (h264_rkmpp, constant
# QP 10: a near-lossless intermediate) so the CPU stays free for rendering.
# record.py drops KWin's frame limiter, keeps frames at least 15 ms apart
# with KWin's timestamps, and enlarges the FIFO to 1 MiB (see record.py); it
# prints the capture rate and the number of missed frames when it stops. KWin
# only sends a frame when the band changes; keepalive-time resends the last
# one every 100 ms so still stretches keep their length. The file is variable
# frame rate; encode.sh resamples it to 60 fps.
# With libmali (CAPTURE_GL set) KWin fills linear DMA-BUFs by a GPU blit and
# GStreamer's GL elements convert and read them back on their own thread
# (--gl). memfd buffers make KWin read back inside its render loop, which caps
# the stream near 37 fps on Mali and flips it upside down.
# stream_start X Y W H: start a region stream, set SERIAL.
stream_start() {
    local i
    rm -f /tmp/stream.out
    sessiontool region "$1" "$2" "$3" "$4" "$SCALE" >/tmp/stream.out 2>&1 &
    STREAM=$!
    for i in $(seq 100); do
        SERIAL=$(sed -n 's/^serial //p' /tmp/stream.out)
        [ -n "$SERIAL" ] && return 0
        sleep 0.1
    done
    echo "screencast stream failed: $(cat /tmp/stream.out)" >&2
    return 1
}
rec_start() {
    local i
    CLIP=$1
    rm -f "/tmp/$CLIP.mkv"
    if [ -n "${GPU:-}" ]; then
        stream_start 0 "$CROP_Y" 1280 640
        rm -f /tmp/rec.fifo /tmp/rec.ready /tmp/rec.trim
        mkfifo /tmp/rec.fifo
        $JF_FFMPEG -nostdin -hide_banner -loglevel warning -blocksize 1048576 -f matroska -i /tmp/rec.fifo \
            -c:v h264_rkmpp -rc_mode CQP -qp_init 10 -g 120 -fps_mode passthrough -y "/tmp/$CLIP.mkv" >/tmp/enc.log 2>&1 &
        ENC=$!
        env $CAPTURE_GL python3 "$HERE/record.py" "$SERIAL" /tmp/rec.fifo ${CAPTURE_GL:+--gl} \
            --ready /tmp/rec.ready --trim /tmp/rec.trim >/tmp/gst.log 2>&1 &
        REC=$!
        # Live once the first frame is in; ffmpeg probes its input for a few
        # seconds before it writes anything, but it reads (and keeps) the
        # frames meanwhile, so there is no need to wait for it.
        for i in $(seq 200); do
            kill -0 "$REC" 2>/dev/null && kill -0 "$ENC" 2>/dev/null || { cat /tmp/gst.log /tmp/enc.log >&2; return 1; }
            [ -e /tmp/rec.ready ] && break
            sleep 0.02
        done
        kill -USR1 "$REC"   # trim start (see record.py --trim)
        return
    fi
    # x11grab paces itself on the (slowed) session clock and stamps frames with
    # it; the band below the top 80 logical px is recorded at physical size.
    # 120 fps: KWin repaints at 60 Hz on its own phase, so a 60 Hz grab beats
    # against it (about 40 distinct frames a second, the rest duplicates);
    # grabbing twice as often catches every repaint within 8 ms. encode.sh
    # resamples to 60 fps.
    ffmpeg -nostdin -loglevel error -f x11grab -framerate 120 -video_size "$(p 1280)x$(p 640)" \
        -draw_mouse 1 -i ":99+0,$(p "$CROP_Y")" -c:v utvideo -pix_fmt gbrp -y "/tmp/$CLIP.mkv" &
    REC=$!
    for i in $(seq 100); do
        [ "$(stat -c %s "/tmp/$CLIP.mkv" 2>/dev/null || echo 0)" -gt 4000000 ] && break
        sleep 0.1
    done
    sleep 0.3
}
rec_stop() {
    [ -n "${GPU:-}" ] && kill -USR1 "$REC"   # trim end
    kill -INT "$REC"
    wait "$REC" || true
    if [ -n "${GPU:-}" ]; then
        wait "$ENC" || cat /tmp/enc.log >&2   # ends at the Matroska EOF from record.py's EOS
        grep -h 'record.py:' /tmp/gst.log >&2 || true
        kill "$STREAM" 2>/dev/null || true
        wait "$STREAM" 2>/dev/null || true
        cp /tmp/rec.trim "$RAW/$CLIP.trim"
    fi
    cp "/tmp/$CLIP.mkv" "$RAW/$CLIP.mkv"
    rm -f "/tmp/$CLIP.mkv"
    echo "recorded $CLIP"
}

kremarc() {   # kremarc KEY=VALUE...: write the demo config plus overrides
    local pinned="" l
    for l in "${LAUNCHERS[@]}"; do pinned+="applications:$l.desktop,"; done
    {
        echo "[General]"
        echo "IconSize=72"
        echo "AttentionAnimationDuration=3"
        # Text tooltips by default: on session.sh KWin cannot offer
        # screencasts (QPainter compositing), so previews would only show
        # placeholder icons. The GPU clips previews and groups turn them on.
        echo "PreviewEnabled=false"
        echo "PinnedLaunchers=${pinned%,}"
        printf '%s\n' "$@"
    } >~/.config/kremarc
}
start_krema() {
    pkill -x krema 2>/dev/null || true
    while pgrep -x krema >/dev/null; do sleep 0.05; done
    # MOZ_LEGACY_PROFILES: Firefox (launched by Krema) uses the profile from setup.
    MOZ_LEGACY_PROFILES=1 krema >>/tmp/krema.log 2>&1 &
    # Ready when KWin lists the dock's layer surface: by then its first frame
    # (icons included) is on screen, about 1 s after the start.
    kwin_until "for (const w of workspace.windowList()) if (w.resourceClass == 'krema' && w.dock) print('DOCK-UP');" DOCK-UP
    sleep 0.2
}

firefox_profile() {
    local dir
    rm -rf ~/.config/mozilla
    (unset LD_PRELOAD; firefox --headless -CreateProfile clips >/dev/null 2>&1) || true
    dir=$(dirname "$(ls -d ~/.config/mozilla/firefox/*.clips/times.json)")
    sed -i 's/^Path=\(.*\)$/Path=\1\nDefault=1/' ~/.config/mozilla/firefox/profiles.ini
    cat >"$dir/user.js" <<'EOF'
user_pref("layout.frame_rate", 60);
user_pref("browser.startup.page", 1);
user_pref("browser.startup.homepage", "about:home");
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("browser.aboutwelcome.enabled", false);
user_pref("startup.homepage_welcome_url", "");
user_pref("startup.homepage_welcome_url.additional", "");
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("datareporting.policy.dataSubmissionPolicyBypassNotification", true);
user_pref("toolkit.telemetry.reportingpolicy.firstRun", false);
user_pref("browser.tabs.warnOnClose", false);
user_pref("browser.sessionstore.resume_from_crash", false);
user_pref("browser.newtabpage.activity-stream.showSponsored", false);
user_pref("browser.newtabpage.activity-stream.showSponsoredTopSites", false);
user_pref("browser.newtabpage.activity-stream.feeds.section.topstories", false);
user_pref("browser.newtabpage.activity-stream.feeds.topsites", false);
user_pref("browser.newtabpage.activity-stream.showWeather", false);
EOF
}

# KWin window rules: new windows of these apps open at fixed spots inside the
# recorded band, unmaximized, so no window jumps after it maps. (A rule, not a
# KWin script: scripts run before KWin's own placement, which then moves the
# window.)  rule CLASS X Y W H [SIZERULE]: SIZERULE 3 = apply once (default),
# 2 = force (Firefox resizes itself to the screen after it maps otherwise).
rule() {
    cat <<EOF

[$1]
Description=$1 placement
wmclass=$1
wmclassmatch=2
position=$2,$3
positionrule=3
size=$4,$5
sizerule=${6:-3}
maximizehoriz=false
maximizehorizrule=2
maximizevert=false
maximizevertrule=2
fullscreen=false
fullscreenrule=2
EOF
}
window_rules() {
    {
        printf '[General]\ncount=3\nrules=firefox,konsole,kate\n'
        rule firefox 330 110 620 440 2
        rule konsole 360 180 560 360
        rule kate 330 110 620 440
    } >~/.config/kwinrulesrc
    busctl --user call org.kde.KWin /KWin org.kde.KWin reconfigure
}

setup() {
    mkdir -p Desktop Documents Downloads Pictures ~/.config
    kremarc
    firefox_profile
    window_rules
    /usr/libexec/kactivitymanagerd >/tmp/kamd.log 2>&1 &
    for _ in $(seq 100); do busctl --user status org.kde.ActivityManager >/dev/null 2>&1 && break; sleep 0.05; done
    plasmashell --no-respawn >/tmp/plasmashell.log 2>&1 &
    # Ready once the desktop and the default panel exist (the panel has to be
    # there to be removed) and the notification server (the panel's system
    # tray starts it; the attention clip needs it) owns its name.
    local js="print(desktops().length + ' ' + panels().length)" state
    for _ in $(seq 300); do
        state=$(busctl --user call org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell evaluateScript s "$js" 2>/dev/null || true)
        case $state in
        *'"'[1-9]*' '[1-9]*) busctl --user status org.freedesktop.Notifications >/dev/null 2>&1 && break ;;
        esac
        sleep 0.1
    done
    # Drop the default panel so Krema is the only dock; set the wallpaper.
    busctl --user call org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell evaluateScript s \
      "panels().forEach(function(p){p.remove()}); desktops().forEach(function(d){d.wallpaperPlugin='org.kde.image'; d.currentConfigGroup=['Wallpaper','org.kde.image','General']; d.writeConfig('Image','$WALLPAPER'); d.writeConfig('FillMode', 2)});" >/dev/null
    demo_files
    start_krema
    move 640 300
    echo SETUP-DONE
}

geometry() {   # a still of the whole screen in /out/geometry.png
    if [ -n "${GPU:-}" ]; then
        stream_start 0 0 "$W" "$H"
        # The first frame of a new stream is complete; later ones come only on
        # damage. With libmali the frame comes as a DMA-BUF (memfd frames
        # are upside down on GLES, see rec_start).
        if [ -n "${CAPTURE_GL:-}" ]; then
            env $CAPTURE_GL gst-launch-1.0 -q pipewiresrc target-object="$SERIAL" num-buffers=1 ! \
                "video/x-raw(memory:DMABuf),format=DMA_DRM,drm-format=AR24" ! glupload ! glcolorconvert ! \
                "video/x-raw(memory:GLMemory),format=RGBA" ! gldownload ! videoconvert ! video/x-raw,format=RGB ! \
                pngenc ! filesink location=/out/geometry.png
        else
            gst-launch-1.0 -q pipewiresrc target-object="$SERIAL" num-buffers=1 ! video/x-raw ! videoconvert ! \
                pngenc ! filesink location=/out/geometry.png
        fi
        kill "$STREAM"; wait "$STREAM" 2>/dev/null || true
    else
        ffmpeg -loglevel error -f x11grab -video_size "$(p 1280)x$(p "$H")" -i :99 -frames:v 1 -y /out/geometry.png
    fi
    echo "wrote /out/geometry.png"
}

# Open Krema's settings window (dock context menu -> Settings...) and place it
# above the dock. While it is open the dock has a ninth entry (the settings
# window, Zoom K icon), which shifts every icon left by half a slot.
open_settings() {
    move "$(icon_x $SYSSETTINGS)" "$ICON_Y"; sleep 0.3
    xdotool click 3
    kwin_until "for (const w of workspace.windowList()) if (w.resourceClass == 'krema' && w.popupWindow) print('MENU-UP');" MENU-UP
    sleep 0.2
    # Bottom-up in the menu: Quit, About Krema, Settings...
    xdotool key Up Up Up Return
    kwin_until "for (const w of workspace.windowList()) if (w.normalWindow && w.caption.indexOf('Krema') >= 0) {
        w.frameGeometry = {x: 300, y: 92, width: 680, height: 470}; workspace.activeWindow = w; print('SETTINGS-PLACED'); }" SETTINGS-PLACED
    sleep 0.5
    move 1150 300
}
close_settings() {
    kwin_js "for (const w of workspace.windowList()) if (w.normalWindow && w.caption.indexOf('Krema') >= 0) w.closeWindow();"
    kwin_until "let n = 0; for (const w of workspace.windowList()) if (w.normalWindow && w.caption.indexOf('Krema') >= 0) n++; if (n == 0) print('SETTINGS-GONE');" SETTINGS-GONE
}

# 1. Parabolic zoom: enter from the upper left, sweep right and back, leave.
clip_zoom() {
    local l r
    l=$(( $(icon_x 0) - 20 )) r=$(( $(icon_x 7) + 20 ))
    move 260 420
    rec_start zoom
    sleep 0.5
    glide 260 420 "$l" "$ICON_Y" 0.8
    glide "$l" "$ICON_Y" "$r" "$ICON_Y" 2.4
    glide "$r" "$ICON_Y" "$l" "$ICON_Y" 2.4
    glide "$l" "$ICON_Y" 260 420 0.8
    sleep 0.8
    rec_stop
}

# 2. Launch: click Firefox -> launch bounce -> window opens -> running dot.
#    Firefox's cold start is long enough for a few bounces even on the slowed
#    clock; its profile (see firefox_profile) opens the offline start page.
#    The window is closed at the end so the loop ends where it began.
# In the GPU session the app is Kate: Firefox resizes itself to nearly the
# whole screen there despite the forced-size rule.
clip_launch() {
    local x app=firefox i=$FIREFOX
    [ -z "${GPU:-}" ] || { app=kate; i=$KATE; }
    x=$(icon_x "$i")
    move 300 440
    rec_start launch
    sleep 0.4
    glide 300 440 "$x" "$ICON_Y" 0.8
    sleep 0.3
    xdotool click 1
    sleep 0.1
    glide "$x" "$ICON_Y" 300 440 0.8
    wait_class "$app" 1
    sleep 2.5
    close_class "$app"
    sleep 1.2
    rec_stop
    pkill -x "$app" 2>/dev/null || true   # Firefox's background processes outlive the window
}

# Files for the Dolphin windows and the pin clip.
demo_files() {
    local i
    mkdir -p Documents/Notes Documents/Invoices Downloads Pictures/Wallpapers Music Desktop
    for i in "Release checklist" "Meeting notes" "Packaging TODO" "Ideas"; do
        printf '%s\n' "$i" >"Documents/$i.txt"
    done
    find /usr/share/wallpapers -path '*contents/images/*' -name '*.png' | head -n 4 |
        while read -r i; do cp "$i" "Pictures/$(echo "$i" | cut -d/ -f5).png"; done
    for i in krema-0.8.0.tar.gz plasma-notes.pdf fedora-44.iso; do head -c 4096 /dev/urandom >"Downloads/$i"; done
    cp /usr/share/applications/org.kde.elisa.desktop Desktop/
    chmod +x Desktop/org.kde.elisa.desktop   # trusted: Dolphin shows its name and icon
}

# 3. Attention: three notifications for Konsole (desktop-entry hint) -> badge
#    counts 1, 2, 3 and the Wiggle attention animation plays. Closing the
#    notifications clears the badge again.
clip_attention() {
    local ids=() i
    move 640 60   # pointer outside the recorded band
    rec_start attention
    sleep 0.6
    for i in 1 2 3; do
        ids+=("$(notify-send -p -t 60000 -a Konsole -i utilities-terminal \
            --hint=string:desktop-entry:org.kde.konsole "Build $i of 3 finished" "krema: ninja done")")
        sleep 1.0
    done
    sleep 2.4
    for i in "${ids[@]}"; do
        busctl --user call org.freedesktop.Notifications /org/freedesktop/Notifications \
            org.freedesktop.Notifications CloseNotification u "$i"
    done
    sleep 1.0
    rec_stop
}

# Dolphin windows on three folders, cascaded; the last one is on top.
open_dolphins() {
    local d i=0
    for d in Documents Downloads Pictures; do
        dolphin --new-window "$HOME/$d" >/dev/null 2>&1 &
        # Placed once the window shows its folder in the caption.
        kwin_until "for (const w of workspace.windowList()) if (w.normalWindow && w.resourceClass.indexOf('dolphin') >= 0 && w.caption.indexOf('$d') == 0) {
            w.frameGeometry = {x: $((250 + 90 * i)), y: $((100 + 36 * i)), width: 600, height: 400}; workspace.activeWindow = w; print('DOLPHIN-$d'); }" "DOLPHIN-$d"
        i=$((i + 1))
    done
    sleep 0.3
}

# 5. Wheel: three Dolphin windows; the wheel over the Dolphin icon brings each
#    one to the front in turn, and back to the first.
clip_wheel() {
    local x i
    open_dolphins
    x=$(icon_x $DOLPHIN)
    move 1000 300
    rec_start wheel
    sleep 0.5
    glide 1000 300 "$x" "$ICON_Y" 1.0
    sleep 0.6
    for i in 1 2 3; do xdotool click 5; sleep 1.1; done
    glide "$x" "$ICON_Y" 1000 300 1.0
    sleep 0.5
    rec_stop
    close_class dolphin
    wait_gone dolphin
}

# 6. Middle click: a middle click on the running Konsole opens a second window.
clip_middle() {
    local x
    konsole >/dev/null 2>&1 &
    wait_class konsole 1
    sleep 2
    place konsole 200 110 560 360
    sleep 1
    x=$(icon_x $KONSOLE)
    move 1000 300
    rec_start middle
    sleep 0.5
    glide 1000 300 "$x" "$ICON_Y" 1.0
    sleep 0.4
    xdotool click 2
    # Leave before the new window maps: Krema opens the hover popup when a
    # hovered entry gains a window (main.qml onRowsInserted), even with
    # PreviewEnabled=false, and here it could only show placeholder icons.
    glide "$x" "$ICON_Y" 1000 300 0.3
    wait_class konsole 2
    sleep 1.8
    # Close the new window only, so the loop ends with the one it began with.
    kwin_js "let l = workspace.windowList().filter(w => w.normalWindow && w.resourceClass.indexOf('konsole') >= 0); l[l.length - 1].closeWindow();"
    sleep 1.2
    rec_stop
    close_class konsole
    wait_gone konsole
}

# 7. Reorder: press and hold Kate, drag it two places right, drop; then drag it
#    back so the loop ends where it began.
# drag X1 Y1 X2 Y2 SECONDS [HOLD]: press, hold HOLD s (default 0.5; Krema's
# press-and-hold is 300 ms), glide, wait 0.3 s, release. On the GPU session
# all of it goes through one X connection (glide.py --hold): KWin releases a
# button about 1 s after the XTEST client that pressed it exits, so after an
# `xdotool mousedown` the drag would end partway through the glide (see
# NOTES.md).
drag() {
    local hold=${6:-0.5}
    if [ -n "${GPU:-}" ]; then
        python3 "$HERE/glide.py" "$(p "$1")" "$(p "$2")" "$(p "$3")" "$(p "$4")" "$5" --hold "$hold" 2>/dev/null
        return
    fi
    move "$1" "$2"
    xdotool mousedown 1
    sleep "$hold"
    glide "$1" "$2" "$3" "$4" "$5"
    sleep 0.3
    xdotool mouseup 1
}
clip_reorder() {
    local a b
    a=$(icon_x $KATE) b=$(( $(icon_x $((KATE + 2))) + 10 ))
    move 1000 300
    rec_start reorder
    sleep 0.5
    glide 1000 300 "$a" "$ICON_Y" 1.0
    sleep 0.3
    drag "$a" "$ICON_Y" "$b" "$ICON_Y" 1.2
    sleep 0.65
    # Krema keeps the hovered index from before a drag until the pointer moves
    # again (hover tracking pauses while dragging), so a press right after the
    # drop would pick up the icon now in Kate's old slot. A small move first
    # re-targets Kate, then drag it back.
    glide "$b" "$ICON_Y" "$(( b - 6 ))" "$ICON_Y" 0.15
    drag "$(( b - 6 ))" "$ICON_Y" "$(( a - 10 ))" "$ICON_Y" 1.2
    sleep 0.3
    glide "$(( a - 10 ))" "$ICON_Y" 1000 300 1.0
    sleep 0.5
    rec_stop
}

# 8. Pin: drag Elisa's .desktop file from a Dolphin window onto the dock (it
#    becomes a pinned launcher), then drag the new icon off the dock (unpinned).
clip_pin() {
    local fx fy tx
    dolphin --new-window "$HOME/Desktop" >/dev/null 2>&1 &
    wait_class dolphin 1
    sleep 2
    place dolphin 380 130 520 330
    sleep 1
    # The first file icon in Dolphin's icon view (read from a still).
    fx=${PIN_FILE_X:-597} fy=${PIN_FILE_Y:-250}
    tx=$(( $(icon_x 7) + 50 ))
    move 1000 250
    rec_start pin
    sleep 0.5
    glide 1000 250 "$fx" "$fy" 0.9
    sleep 0.3
    xdotool mousedown 1
    sleep 0.2
    glide "$fx" "$fy" "$((fx + 20))" "$((fy + 20))" 0.2
    glide "$((fx + 20))" "$((fy + 20))" "$tx" "$ICON_Y" 1.4
    sleep 0.6
    xdotool mouseup 1
    sleep 1.4
    # The new launcher is the ninth entry; the dock re-centred, so it sits at
    # icon_x(8) - ICON_STEP/2. Drag it off the dock again.
    local nx=$(( $(icon_x 8) - ICON_STEP / 2 ))
    drag "$nx" "$ICON_Y" "$nx" 420 1.0
    sleep 0.3
    glide "$nx" 420 1000 250 0.8
    sleep 0.6
    rec_stop
    close_class dolphin
    sleep 1
}

# 9. Settings: Icon size 72 -> 88 with the spin box arrows and back; the dock
#    resizes live. The window is opened and placed before recording.
UP_X=931 UP_Y=244 DOWN_Y=257
clip_settings() {
    local i
    open_settings
    rec_start settings
    sleep 0.4
    glide 1150 300 "$UP_X" "$UP_Y" 0.8
    for i in 1 2 3 4; do sleep 0.45; xdotool click 1; done
    sleep 1.0
    glide "$UP_X" "$UP_Y" "$UP_X" "$DOWN_Y" 0.25
    for i in 1 2 3 4; do sleep 0.45; xdotool click 1; done
    sleep 0.5
    glide "$UP_X" "$DOWN_Y" 1150 300 0.8
    sleep 0.6
    rec_stop
    close_settings
}

# 10. Styles: Background style Panel Inherit -> Tinted -> Acrylic ->
#     Transparent -> Panel Inherit with the combo box in the settings window;
#     the dock repaints right away. The page is scrolled to the Background
#     group (wheel over the scroll bar, not over a control) before recording.
# Combo box, and the rows of its popup list (it opens below the box). Measured
# with the page scrolled STYLE_SCROLL wheel steps (Krema 0.9 Appearance page).
# Transparent hides the Opacity and accent rows; at this scroll position the
# page is still long enough not to scroll back, so the combo never moves.
STYLE_X=744 STYLE_Y=329 ITEM_X=600 STYLE_SCROLL=12
declare -A STYLE_ROW=([panel]=367 [transparent]=403 [tinted]=438 [acrylic]=474)
pick_style() {   # pick_style NAME: open the combo box and click the entry
    local y=${STYLE_ROW[$1]}
    xdotool click 1; sleep 0.6
    glide "$STYLE_X" "$STYLE_Y" "$ITEM_X" "$y" 0.45
    sleep 0.2
    xdotool click 1; sleep 0.3
    glide "$ITEM_X" "$y" "$STYLE_X" "$STYLE_Y" 0.4
}
clip_styles() {
    local i
    open_settings
    move 970 400
    for i in $(seq "$STYLE_SCROLL"); do xdotool click 5; sleep 0.15; done
    sleep 0.5
    move 1150 300
    rec_start styles
    sleep 0.4
    glide 1150 300 "$STYLE_X" "$STYLE_Y" 0.8
    sleep 0.2
    pick_style tinted;      sleep 1.2
    pick_style acrylic;     sleep 1.2
    pick_style transparent; sleep 1.2
    pick_style panel;       sleep 0.6
    glide "$STYLE_X" "$STYLE_Y" 1150 300 0.8
    sleep 0.5
    rec_stop
    close_settings
}

# 11. Auto-hide: the dock is hidden, the pointer hits the bottom edge, the dock
#     slides in, zooms under the pointer, and slides out after it leaves.
clip_autohide() {
    kremarc VisibilityMode=1
    start_krema
    move 640 380; sleep 1.2   # the dock hides again after its start
    rec_start autohide
    sleep 0.7
    glide 640 380 600 719 0.8
    sleep 1.2
    glide 600 719 760 "$ICON_Y" 0.9
    sleep 0.4
    glide 760 "$ICON_Y" 640 380 0.8
    sleep 1.9
    rec_stop
    kremarc
    start_krema
}

# 12. Dodge: VisibilityMode=2. Dragging a Kate window down over the dock makes
#     the dock slide away; dragging it back up brings the dock back.
clip_dodge() {
    kremarc VisibilityMode=2
    start_krema
    kate >/dev/null 2>&1 &
    wait_class kate 1
    sleep 1
    place kate 330 110 620 440
    sleep 0.3
    # Title bar of the Kate window, 14 px below its top edge.
    move 1000 300
    rec_start dodge
    sleep 0.5
    glide 1000 300 760 124 0.9
    drag 760 124 760 284 1.2 0.2
    sleep 1.3
    drag 760 284 760 124 1.2 0.2
    glide 760 124 1000 300 0.8
    sleep 1.2
    rec_stop
    close_class kate
    wait_gone kate
    kremarc
    start_krema
}

# 13. Keyboard: Meta+F5 focuses the dock, the arrow keys move the focus ring,
#     Escape leaves keyboard navigation.
clip_keyboard() {
    local i
    move 640 60
    rec_start keyboard
    sleep 0.6
    # Meta+F5. xdotool's Super modifier does not reach KWin's global-shortcut
    # handling in the X11-windowed backend, so ask kglobalaccel to trigger
    # Krema's registered "focus-dock" action (what Meta+F5 is bound to). The
    # arrow keys and Escape below are real key presses.
    busctl --user call org.kde.kglobalaccel /component/krema org.kde.kglobalaccel.Component \
        invokeShortcut s focus-dock
    sleep 0.9
    for i in 1 2 3 4; do xdotool key Right; sleep 0.55; done
    for i in 1 2; do xdotool key Left; sleep 0.55; done
    sleep 0.4
    xdotool key Escape
    sleep 1.0
    rec_stop
}

# ---- GPU session only (session-gpu.sh): live PipeWire window thumbnails ----

# Konsole windows with moving content, so the thumbnails visibly update.
# konsole_run X Y W H CMD...: open one and place it (the new window is active).
konsole_run() {
    local n g="{x: $1, y: $2, width: $3, height: $4}"
    shift 4
    n=$(count_class konsole)
    konsole --separate --hide-menubar --hide-tabbar -e "$@" >/dev/null 2>&1 &
    wait_class konsole $((n + 1))
    kwin_until "const l = workspace.windowList().filter(w => w.normalWindow && w.resourceClass.indexOf('konsole') >= 0);
        if (l.length == $((n + 1))) { const w = l[l.length - 1]; w.frameGeometry = $g; workspace.activeWindow = w; print('KONSOLE-$((n + 1))'); }" "KONSOLE-$((n + 1))"
    sleep 0.5
}
TOP=(top -d 0.4)
CLOCK=(bash -c 'while :; do printf "\e[1;3%dm%s\e[0m  build step %d ok\n" $((RANDOM % 6 + 1)) "$(date +%T.%N)" $RANDOM; sleep 0.06; done')
LOG=(bash -c 'while :; do for f in /usr/share/applications/*.desktop; do printf "\e[32mcompiling\e[0m %s\n" "${f##*/}"; sleep 0.09; done; done')

# Preview popup geometry (logical px), measured from a still with the popup
# open (`clips.sh geometry`). Thumbnails are PreviewThumbnailSize=200 wide.
# THUMB_X[i]: centre of thumbnail i; THUMB_Y: its centre; CLOSE_DX/DY: the
# close button relative to that centre.
THUMB_Y=${THUMB_Y:-468}
THUMB_X1=${THUMB_X1:-504}   # single-window popup
THUMB_X=(${THUMB_XS:-291 504 715})
CLOSE_DX=${CLOSE_DX:-83} CLOSE_DY=${CLOSE_DY:-50}

# 14. Previews: hover the running Konsole -> the popup opens with a live
#     thumbnail of the window (top refreshing); move onto the thumbnail and
#     back out.
clip_previews() {
    local x
    kremarc PreviewEnabled=true
    start_krema
    konsole_run 360 150 560 360 "${TOP[@]}"
    x=$(icon_x $KONSOLE)
    move 1000 300
    sleep 0.3
    rec_start previews
    sleep 0.5
    glide 1000 300 "$x" "$ICON_Y" 1.0
    sleep 2.4
    glide "$x" "$ICON_Y" "$THUMB_X1" "$THUMB_Y" 0.6
    sleep 2.0
    glide "$THUMB_X1" "$THUMB_Y" 1000 300 0.9
    sleep 1.2
    rec_stop
    close_class konsole
    wait_gone konsole
}

# 15. Groups: three Konsole windows -> the popup shows three live thumbnails.
#     A click on the middle one brings its window to the front; the popup
#     opens again and a click on the third brings that one back on top, which
#     restores the starting stack (3 over 2 over 1), so the loop ends where it
#     began. The terminals update slowly (top every 1 s, one line every
#     0.5 s): fast-scrolling text does not fit the size budget.
GROUP_TOP=(top -d 1)
GROUP_CLOCK=(bash -c 'while :; do printf "\e[1;3%dm%s\e[0m  build step %d ok\n" $((RANDOM % 6 + 1)) "$(date +%T)" $RANDOM; sleep 0.5; done')
GROUP_LOG=(bash -c 'while :; do for f in /usr/share/applications/*.desktop; do printf "\e[32mcompiling\e[0m %s\n" "${f##*/}"; sleep 0.5; done; done')
clip_groups() {
    local x
    kremarc PreviewEnabled=true
    start_krema
    konsole_run 120 120 520 330 "${GROUP_CLOCK[@]}"
    konsole_run 380 150 520 330 "${GROUP_LOG[@]}"
    konsole_run 640 180 520 330 "${GROUP_TOP[@]}"
    x=$(icon_x $KONSOLE)
    move 1100 200
    sleep 0.3
    rec_start groups
    sleep 0.4
    glide 1100 200 "$x" "$ICON_Y" 0.8
    sleep 1.3
    glide "$x" "$ICON_Y" "${THUMB_X[1]}" "$THUMB_Y" 0.5
    sleep 0.3
    xdotool click 1
    sleep 0.8
    glide "${THUMB_X[1]}" "$THUMB_Y" "$x" "$ICON_Y" 0.4
    sleep 1.5   # the reopened popup restarts its streams: placeholders first
    glide "$x" "$ICON_Y" "${THUMB_X[2]}" "$THUMB_Y" 0.5
    sleep 0.3
    xdotool click 1
    sleep 0.7
    glide "${THUMB_X[2]}" "$THUMB_Y" 1100 200 0.7
    sleep 0.7
    rec_stop
    close_class konsole
    wait_gone konsole
}

# 16. Progress: Dolphin (pinned, not running) reports a transfer through the
#     Unity LauncherEntry API: a count badge (3 files left, counting down) and
#     a progress bar filling from 0 to 100 %; both clear when it is done.
#     unity.py paces the ramps itself: one long-lived process, no per-step
#     spawns.
clip_progress() {
    kremarc
    start_krema
    move 640 60   # pointer outside the recorded band
    exec 5> >(python3 "$HERE/unity.py" org.kde.dolphin.desktop)
    echo "progress-visible=false" >&5
    sleep 1   # python + dbus start-up
    rec_start progress
    sleep 0.6
    echo "count=3 count-visible=true progress=0 progress-visible=true ramp=0:0.333:1.2" >&5
    echo "count=2 ramp=0.333:0.667:1.2" >&5
    echo "count=1 ramp=0.667:1:1.2" >&5
    sleep 4.4
    echo "count=0 count-visible=false progress-visible=false" >&5
    sleep 1.4
    rec_stop
    exec 5>&-
}

# Timing probe (not a clip; `clips.sh timing`): a 4.0 s pointer glide, then a
# 4.0 s progress ramp, with wall-clock markers in /out/raw/timing.log. Compare
# the marker intervals with the motion in /out/raw/timing.mkv.
clip_timing() {
    local log=$RAW/timing.log
    kremarc
    start_krema
    move 100 400
    exec 5> >(python3 "$HERE/unity.py" org.kde.dolphin.desktop 2>>"$log")
    echo "progress-visible=false" >&5
    sleep 1
    : >"$log"
    rec_start timing
    sleep 1
    echo "glide-start $(date +%s.%N)" >>"$log"
    python3 "$HERE/glide.py" "$(p 100)" "$(p 400)" "$(p 1180)" "$(p 400)" 4.0 2>>"$log"
    echo "glide-end $(date +%s.%N)" >>"$log"
    sleep 1
    echo "count=3 count-visible=true progress=0 progress-visible=true ramp=0:1:4" >&5
    sleep 5
    echo "count-visible=false progress-visible=false" >&5
    sleep 1
    rec_stop
    exec 5>&-
    cat "$log"
}

[ $# -gt 0 ] || { echo "usage: $0 setup|all|geometry|$CLIPS"; exit 2; }
for arg in "$@"; do
    case $arg in
        all) setup; for c in $CLIPS; do "clip_$c"; done ;;
        *) if declare -F "clip_$arg" >/dev/null; then "clip_$arg"
           elif declare -F "$arg" >/dev/null; then "$arg"   # setup, geometry, helpers
           else echo "unknown: $arg"; exit 2; fi ;;
    esac
done
