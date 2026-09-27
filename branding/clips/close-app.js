// KWin script: close Firefox so the launch clip ends in the state it started in.
for (const w of workspace.windowList()) {
    if (w.normalWindow && w.resourceClass.indexOf("firefox") >= 0) w.closeWindow();
}
