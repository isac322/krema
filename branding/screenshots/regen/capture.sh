#!/bin/bash
# Runs inside the container after session.sh is READY. Writes PNGs to /out.
# Coordinates assume 1920x1080, IconSize=56, IconSpacing=4 and the 10 pinned
# launchers below: icon i (0-based) is centered at x = 690 + 60*i, y ~= 1035.
set -eu
. /tmp/session.env
export DISPLAY=:99
cd "$HOME"
mkdir -p Desktop Documents Downloads Music Pictures Videos Projects/krema
cp -r /repo/README.md /repo/CMakeLists.txt /repo/CHANGELOG.md /repo/LICENSE /repo/src /repo/packaging Projects/krema/
mkdir -p ~/.config
cat >~/.config/kremarc <<EOF
[General]
IconSize=56
PinnedLaunchers=applications:org.kde.dolphin.desktop,applications:org.mozilla.firefox.desktop,applications:org.kde.konsole.desktop,applications:org.kde.kate.desktop,applications:org.kde.okular.desktop,applications:org.kde.gwenview.desktop,applications:org.kde.elisa.desktop,applications:org.kde.kcalc.desktop,applications:org.kde.plasma-systemmonitor.desktop,applications:systemsettings.desktop
EOF

# kactivitymanagerd is systemd-activated on real sessions; plasmashell aborts without it.
/usr/libexec/kactivitymanagerd >/tmp/kamd.log 2>&1 &
sleep 1
plasmashell --no-respawn >/tmp/plasmashell.log 2>&1 &
for _ in $(seq 60); do busctl --user status org.kde.plasmashell >/dev/null 2>&1 && break; sleep 1; done
sleep 10
# Wallpaper only: drop the default panel so Krema is the only dock; use Plasma's "Next" wallpaper.
busctl --user call org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell evaluateScript s \
  'panels().forEach(function(p){p.remove()}); desktops().forEach(function(d){d.wallpaperPlugin="org.kde.image"; d.currentConfigGroup=["Wallpaper","org.kde.image","General"]; d.writeConfig("Image","file:///usr/share/wallpapers/Next/")});' >/dev/null

krema >/tmp/krema.log 2>&1 &
sleep 8
dolphin "$HOME" >/tmp/dolphin.log 2>&1 &
konsole --workdir "$HOME/Projects/krema" >/tmp/konsole.log 2>&1 &
kate -n "$HOME/Projects/krema/src/app/application.cpp" >/tmp/kate.log 2>&1 &
sleep 12
/regen/kwinscript.sh /regen/place.js >/dev/null
sleep 1
xdotool mousemove 1300 700 click 1
sleep 0.5
xdotool key ctrl+l
if rpm -q krema >/dev/null 2>&1; then xdotool type --delay 20 "rpm -q krema"; xdotool key Return; fi
xdotool type --delay 20 "ls"; xdotool key Return
xdotool mousemove 960 20
sleep 2
scrot -o /out/dock-overview.png

# Parabolic zoom: sweep the pointer along the dock and stop over Okular (i=4).
for x in $(seq 700 10 950); do xdotool mousemove "$x" 1035; sleep 0.02; done
sleep 1.2
scrot -p -o /out/dock-zoom.png

# Settings: right-click System Settings (i=9) -> "Settings..." context-menu entry.
xdotool mousemove 960 400; sleep 1
xdotool mousemove 1240 1035; sleep 0.6
xdotool click 3; sleep 1.5
xdotool mousemove 1290 998; sleep 0.3
xdotool click 1; sleep 8
/regen/kwinscript.sh /regen/settings.js >/dev/null
xdotool mousemove 960 20
sleep 2
scrot -o /out/settings.png
echo CAPTURED
