// KWin script: close the Krema settings window after the settings clip.
for (const w of workspace.windowList()) {
    if (w.normalWindow && w.caption.indexOf("Krema") >= 0) w.closeWindow();
}
