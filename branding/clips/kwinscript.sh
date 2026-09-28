#!/bin/bash
# usage: kwinscript.sh FILE.js [NAME]
#   Without NAME: load, run once, and unload the script.
#   With NAME:    load and run it, and leave it loaded (for scripts that connect
#                 to workspace signals); unload later with
#                 busctl --user call org.kde.KWin /Scripting org.kde.kwin.Scripting unloadScript s NAME
. /tmp/session.env
name="${2:-clip$RANDOM$RANDOM}"
f=$1
if [ -z "${2:-}" ]; then
    # KWin runs the script after `run` returns. Its last line prints a marker
    # to KWin's log (js.debug, see session-gpu.sh), and the script is unloaded
    # once that shows up instead of after a fixed second.
    f=$(mktemp /tmp/kwinXXXX.js)
    { cat "$1"; printf "\nprint('done-%s');\n" "$name"; } >"$f"
fi
id=$(busctl --user call org.kde.KWin /Scripting org.kde.kwin.Scripting loadScript ss "$f" "$name" | awk '{print $2}')
busctl --user call org.kde.KWin "/Scripting/Script$id" org.kde.kwin.Script run
if [ -z "${2:-}" ]; then
    for _ in $(seq 300); do
        tail -c 65536 /tmp/kwin.log | grep -q "done-$name" && break
        sleep 0.01
    done
    busctl --user call org.kde.KWin /Scripting org.kde.kwin.Scripting unloadScript s "$name" >/dev/null
    rm -f "$f"
fi
