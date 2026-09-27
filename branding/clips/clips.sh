#!/bin/bash
# Runs inside the container after session.sh prints READY.
#   usage: clips.sh setup            start plasmashell + Krema with the demo config
#          clips.sh CLIP...          record clips (zoom launch attention settings autohide)
#          clips.sh all              setup + every clip, in that order
# Raw recordings (lossless Ut Video, full 1920x1080 at 60 fps) go to /out/raw;
# encode.sh on the host crops them to 1280x640 at (320,440) and encodes them.
#
# Geometry (1920x1080, scale 1, IconSize=80, IconSpacing=4, 8 launchers):
# icon i (0-based) is centered at x = 666 + 84*i, y = ICON_Y.
set -eu
. /tmp/session.env
export DISPLAY=:99
cd "$HOME"
HERE=$(cd "$(dirname "$0")" && pwd)
RAW=/out/raw
mkdir -p "$RAW"

ICON_Y=${ICON_Y:-1020}
icon_x() { echo $((666 + 84 * $1)); }
FIREFOX=1 SYSSETTINGS=7

# Pointer glide: glide X1 Y1 X2 Y2 SECONDS. Smoothstep easing at 30 Hz, run as
# one xdotool command chain so the pacing comes from xdotool's own sleep, which
# runs on the session's slowed clock (Python's clock is not slowed by libfaketime).
glide() {
    xdotool $(awk -v a="$1" -v b="$2" -v c="$3" -v d="$4" -v t="$5" 'BEGIN {
        n = int(t * 30 + 0.5); if (n < 1) n = 1
        for (i = 1; i <= n; i++) { s = i / n; s = s * s * (3 - 2 * s)
            printf "mousemove %d %d sleep 0.0333 ", a + (c - a) * s + 0.5, b + (d - b) * s + 0.5 } }')
}

rec_start() {
    local i
    CLIP=$1
    rm -f "/tmp/$CLIP.mkv"
    # Timestamps are taken on arrival from the (slowed) session clock, so the
    # recording plays back at true speed even when frames arrive unevenly.
    ffmpeg -nostdin -loglevel error -use_wallclock_as_timestamps 1 -f x11grab -framerate 60 \
        -video_size 1920x1080 -draw_mouse 1 -i :99 -c:v utvideo -pix_fmt gbrp -y "/tmp/$CLIP.mkv" &
    REC=$!
    # Continue only once frames are being written (ffmpeg starts slowly on a busy host).
    for i in $(seq 100); do
        [ "$(stat -c %s "/tmp/$CLIP.mkv" 2>/dev/null || echo 0)" -gt 2000000 ] && break
        sleep 0.1
    done
    sleep 0.3
}
rec_stop() {
    kill -INT "$REC"
    wait "$REC" || true
    cp "/tmp/$CLIP.mkv" "$RAW/$CLIP.mkv"
    rm -f "/tmp/$CLIP.mkv"
    echo "recorded $CLIP"
}

start_krema() {
    pkill -x krema 2>/dev/null && sleep 1 || true
    krema >/tmp/krema.log 2>&1 &
    sleep 8
}

setup() {
    mkdir -p Desktop Documents Downloads ~/.config
    cat >~/.config/kremarc <<EOF
[General]
IconSize=80
AttentionAnimationDuration=3
PreviewEnabled=false
PinnedLaunchers=applications:org.kde.dolphin.desktop,applications:org.mozilla.firefox.desktop,applications:org.kde.konsole.desktop,applications:org.kde.kate.desktop,applications:org.kde.okular.desktop,applications:org.kde.gwenview.desktop,applications:org.kde.kcalc.desktop,applications:systemsettings.desktop
EOF
    # kactivitymanagerd is systemd-activated on real sessions; plasmashell aborts without it.
    /usr/libexec/kactivitymanagerd >/tmp/kamd.log 2>&1 &
    sleep 1
    plasmashell --no-respawn >/tmp/plasmashell.log 2>&1 &
    for _ in $(seq 60); do busctl --user status org.kde.plasmashell >/dev/null 2>&1 && break; sleep 1; done
    sleep 10
    # Drop the default panel so Krema is the only dock; use Plasma's "Next" wallpaper.
    busctl --user call org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell evaluateScript s \
      'panels().forEach(function(p){p.remove()}); desktops().forEach(function(d){d.wallpaperPlugin="org.kde.image"; d.currentConfigGroup=["Wallpaper","org.kde.image","General"]; d.writeConfig("Image","file:///usr/share/wallpapers/Next/")});' >/dev/null
    start_krema
    # KWin window rule: Firefox opens inside the crop, next to its dock icon.
    # (A rule, not a KWin script: scripts run before KWin's own placement, which
    # then moves the window a frame later.)
    cat >~/.config/kwinrulesrc <<EOF
[General]
count=1
rules=firefox

[firefox]
Description=Firefox placement for the launch clip
wmclass=firefox
wmclassmatch=2
position=800,470
positionrule=3
size=640,460
sizerule=3
EOF
    busctl --user call org.kde.KWin /KWin org.kde.KWin reconfigure
    xdotool mousemove 960 540
    sleep 2
    echo SETUP-DONE
}

# 1. Parabolic zoom: enter from the left, sweep right and back, leave where it came in.
clip_zoom() {
    local l r
    l=$(( $(icon_x 0) - 25 )) r=$(( $(icon_x 7) + 25 ))
    xdotool mousemove 560 800
    rec_start zoom
    sleep 0.5
    glide 560 800 "$l" "$ICON_Y" 0.7
    glide "$l" "$ICON_Y" "$r" "$ICON_Y" 2.2
    glide "$r" "$ICON_Y" "$l" "$ICON_Y" 2.2
    glide "$l" "$ICON_Y" 560 800 0.7
    sleep 1.0
    rec_stop
}

# 2. Launch: click Firefox -> launch bounce -> window opens -> running dot. This
#    is Firefox's first start in the session; its ~2 s cold start leaves room
#    for several bounce cycles (fast apps such as KCalc or Kate map within
#    ~0.4 s, before one 8 px bounce completes). Firefox is closed at the end so
#    the loop ends where it began. The pointer leaves the dock right after the
#    click: Krema opens the hover preview when a hovered launcher's window
#    appears (even with PreviewEnabled=false), and this session can only show a
#    placeholder thumbnail there.
clip_launch() {
    local x
    x=$(icon_x $FIREFOX)
    xdotool mousemove 520 760
    rec_start launch
    sleep 0.4
    glide 520 760 "$x" "$ICON_Y" 0.7
    sleep 0.25
    xdotool click 1
    glide "$x" "$ICON_Y" "$x" 860 0.12
    glide "$x" 860 520 760 0.6
    sleep 5.0
    "$HERE/kwinscript.sh" "$HERE/close-app.js" >/dev/null   # includes 1 s settle
    sleep 0.3
    rec_stop
    pkill -x firefox 2>/dev/null || true   # its background processes outlive the window
}

# 3. Attention: three notifications for Konsole (desktop-entry hint) -> badge
#    counts 1, 2, 3 and the default Wiggle attention animation plays. Closing
#    the notifications clears the badge again for a clean loop.
clip_attention() {
    local ids=() i
    xdotool mousemove 960 150   # pointer outside the crop
    rec_start attention
    sleep 0.6
    for i in 1 2 3; do
        ids+=("$(notify-send -p -t 60000 -a Konsole -i utilities-terminal \
            --hint=string:desktop-entry:org.kde.konsole "Build $i of 3 finished" "krema: ninja done")")
        sleep 0.9
    done
    sleep 2.6
    for i in "${ids[@]}"; do
        busctl --user call org.freedesktop.Notifications /org/freedesktop/Notifications \
            org.freedesktop.Notifications CloseNotification u "$i"
    done
    sleep 1.0
    rec_stop
}

# 4. Settings: raise Icon size 80 -> 96 with the spin box arrows and back; the
#    dock resizes live. The settings window is opened (context menu ->
#    Settings...) and placed by settings.js before recording starts; the arrow
#    coordinates below follow from that placement.
ARROW_X=1307 UP_Y=602 DOWN_Y=620
clip_settings() {
    local x i placed
    placed=$(grep -c SETTINGS-PLACED /tmp/kwin.log || true)
    x=$(icon_x $SYSSETTINGS)
    xdotool mousemove "$x" "$ICON_Y"; sleep 0.8
    xdotool click 3; sleep 1.5
    # Bottom-up in the menu: Quit, About Krema, Settings...
    xdotool key Up Up Up Return
    # settings.js prints SETTINGS-PLACED (into KWin's log) once it has found and
    # placed the window; retry until it has, since the window may take a while.
    for i in $(seq 30); do
        sleep 1
        "$HERE/kwinscript.sh" "$HERE/settings.js" >/dev/null
        [ "$(grep -c SETTINGS-PLACED /tmp/kwin.log)" -gt "$placed" ] && break
    done
    sleep 2
    "$HERE/kwinscript.sh" "$HERE/settings.js" >/dev/null   # re-apply after the window settled
    xdotool mousemove 1450 760; sleep 2
    rec_start settings
    sleep 0.4
    glide 1450 760 "$ARROW_X" "$UP_Y" 0.7
    for i in 1 2 3 4; do sleep 0.45; xdotool click 1; done
    sleep 1.0
    glide "$ARROW_X" "$UP_Y" "$ARROW_X" "$DOWN_Y" 0.25
    for i in 1 2 3 4; do sleep 0.45; xdotool click 1; done
    sleep 0.5
    glide "$ARROW_X" "$DOWN_Y" 1450 760 0.7
    sleep 0.6
    rec_stop
    "$HERE/kwinscript.sh" "$HERE/close-settings.js" >/dev/null
}

# 5. Auto-hide: VisibilityMode=1. The dock is hidden, the pointer hits the
#    bottom edge, the dock slides in, the pointer leaves and it slides out.
clip_autohide() {
    sed -i '/^VisibilityMode=/d' ~/.config/kremarc
    sed -i 's/^\[General\]$/[General]\nVisibilityMode=1/' ~/.config/kremarc
    start_krema
    xdotool mousemove 960 700; sleep 2
    rec_start autohide
    sleep 0.7
    glide 960 700 900 1079 0.7
    sleep 1.5
    glide 900 1079 1060 "$ICON_Y" 0.9
    sleep 0.4
    glide 1060 "$ICON_Y" 960 700 0.7
    sleep 1.9
    rec_stop
    sed -i '/^VisibilityMode=/d' ~/.config/kremarc
    start_krema
}

[ $# -gt 0 ] || { echo "usage: $0 setup|all|zoom|launch|attention|settings|autohide..."; exit 2; }
for arg in "$@"; do
    case $arg in
        setup) setup ;;
        all) setup; for c in zoom launch attention settings autohide; do "clip_$c"; done ;;
        zoom|launch|attention|settings|autohide) "clip_$arg" ;;
        *) echo "unknown: $arg"; exit 2 ;;
    esac
done
