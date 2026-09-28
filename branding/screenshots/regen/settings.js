// KWin script: minimize demo apps and center the Krema settings window.
for (const w of workspace.windowList()) {
    if (!w.normalWindow) continue;
    if (w.caption.indexOf("Krema") >= 0) {
        w.minimized = false;
        w.frameGeometry = {x: 400, y: 110, width: 1120, height: 780};
        workspace.activeWindow = w;
    } else {
        w.minimized = true;
    }
}
