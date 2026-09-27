#!/bin/bash
# usage: kwinscript.sh FILE.js [NAME]
#   Without NAME: load, run once, and unload the script.
#   With NAME:    load and run it, and leave it loaded (for scripts that connect
#                 to workspace signals); unload later with
#                 busctl --user call org.kde.KWin /Scripting org.kde.kwin.Scripting unloadScript s NAME
. /tmp/session.env
name="${2:-clip$RANDOM}"
id=$(busctl --user call org.kde.KWin /Scripting org.kde.kwin.Scripting loadScript ss "$1" "$name" | awk '{print $2}')
busctl --user call org.kde.KWin "/Scripting/Script$id" org.kde.kwin.Script run
if [ -z "${2:-}" ]; then
    sleep 1
    busctl --user call org.kde.KWin /Scripting org.kde.kwin.Scripting unloadScript s "$name" >/dev/null
fi
