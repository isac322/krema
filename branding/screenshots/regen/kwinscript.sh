#!/bin/bash
# usage: kwinscript.sh FILE.js — load, run once, and unload a KWin script.
. /tmp/session.env
name="shot$RANDOM"
id=$(busctl --user call org.kde.KWin /Scripting org.kde.kwin.Scripting loadScript ss "$1" "$name" | awk '{print $2}')
busctl --user call org.kde.KWin "/Scripting/Script$id" org.kde.kwin.Script run
sleep 1
busctl --user call org.kde.KWin /Scripting org.kde.kwin.Scripting unloadScript s "$name" >/dev/null
