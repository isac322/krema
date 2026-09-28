#!/bin/bash
# Product stills for branding/screenshots/ in the GPU session: a separate
# session at 1920x1080, scale 1 (thumbs.sh: W=1920 H=1080 SCALE=1). Same
# composition as branding/screenshots/regen/capture.sh (see that NOTES.md),
# with the wallpaper from clips.sh setup. Writes /out/stills/*.png.
# Coordinates: IconSize=56, IconSpacing=4, 10 launchers: icon i is centered
# at x = 690 + 60*i, y ~ 1035.
set -eu
source <(sed '/^\[ \$# -gt 0 \]/,$d' "$(dirname "$0")/clips.sh")
HERE=/clips
OUT=/out/stills
mkdir -p "$OUT" Desktop Documents Downloads Music Pictures Videos Projects/krema ~/.config
for f in README.md CHANGELOG.md LICENSE CMakeLists.txt; do printf '%s\n' "$f" >"Projects/krema/$f"; done
mkdir -p Projects/krema/src Projects/krema/packaging
cat >~/.config/kremarc <<EOF
[General]
IconSize=56
PinnedLaunchers=applications:org.kde.dolphin.desktop,applications:org.mozilla.firefox.desktop,applications:org.kde.konsole.desktop,applications:org.kde.kate.desktop,applications:org.kde.okular.desktop,applications:org.kde.gwenview.desktop,applications:org.kde.elisa.desktop,applications:org.kde.kcalc.desktop,applications:org.kde.plasma-systemmonitor.desktop,applications:systemsettings.desktop
EOF

/usr/libexec/kactivitymanagerd >/tmp/kamd.log 2>&1 &
sleep 1
plasmashell --no-respawn >/tmp/plasmashell.log 2>&1 &
for _ in $(seq 60); do busctl --user status org.kde.plasmashell >/dev/null 2>&1 && break; sleep 1; done
sleep 8
busctl --user call org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell evaluateScript s \
  "panels().forEach(function(p){p.remove()}); desktops().forEach(function(d){d.wallpaperPlugin='org.kde.image'; d.currentConfigGroup=['Wallpaper','org.kde.image','General']; d.writeConfig('Image','$WALLPAPER'); d.writeConfig('FillMode', 2)});" >/dev/null
krema >/tmp/krema.log 2>&1 &
sleep 6
dolphin "$HOME" >/dev/null 2>&1 &
konsole --workdir "$HOME/Projects/krema" >/dev/null 2>&1 &
kate -n >/dev/null 2>&1 &
sleep 10

# still NAME: the whole screen as a PNG, pointer included.
still() {
    geometry >/dev/null
    mv /out/geometry.png "$OUT/$1.png"
    echo "wrote $OUT/$1.png"
}

kwin_js 'const layout = {
    "org.kde.kate": {x: 360, y: 60, width: 1200, height: 700, minimized: true},
    "org.kde.dolphin": {x: 170, y: 110, width: 1000, height: 620},
    "org.kde.konsole": {x: 1010, y: 360, width: 760, height: 470},
};
for (const cls of ["org.kde.kate", "org.kde.dolphin", "org.kde.konsole"]) {
    for (const w of workspace.windowList()) {
        if (w.resourceClass !== cls || !w.normalWindow) continue;
        const g = layout[cls];
        w.minimized = false;
        w.frameGeometry = {x: g.x, y: g.y, width: g.width, height: g.height};
        workspace.activeWindow = w;
        if (g.minimized) w.minimized = true;
    }
}'
sleep 1
xdotool mousemove 1300 700 click 1
sleep 0.5
xdotool type --delay 20 "ls"; xdotool key Return
xdotool mousemove 960 20
sleep 2
still dock-overview

# Parabolic zoom: sweep along the dock and stop over Okular (i=4).
glide 700 1035 930 1035 0.8
sleep 1.5
still dock-zoom

# Settings: right-click System Settings (i=9) -> "Settings..." (bottom-up in
# the menu: Quit, About Krema, Settings...).
xdotool mousemove 960 400; sleep 1
xdotool mousemove 1230 1035; sleep 0.8
xdotool click 3; sleep 1.5
xdotool key Up Up Up Return; sleep 6
kwin_js 'for (const w of workspace.windowList()) {
    if (!w.normalWindow) continue;
    if (w.caption.indexOf("Krema") >= 0) {
        w.minimized = false;
        w.frameGeometry = {x: 400, y: 110, width: 1120, height: 780};
        workspace.activeWindow = w;
    } else {
        w.minimized = true;
    }
}'
xdotool mousemove 960 20
sleep 2
still settings
