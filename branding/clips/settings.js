// KWin script: place the Krema settings window inside the recorded crop
// (1280x640 at x 320, y 440), above the dock. Prints SETTINGS-PLACED to KWin's
// log (category kwin_scripting) when the window exists, so clips.sh can wait for it.
for (const w of workspace.windowList()) {
    if (!w.normalWindow) continue;
    if (w.caption.indexOf("Krema") >= 0) {
        w.minimized = false;
        w.frameGeometry = {x: 560, y: 452, width: 800, height: 470};
        workspace.activeWindow = w;
        print("SETTINGS-PLACED");
    }
}
