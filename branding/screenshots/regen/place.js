// KWin script: place demo windows by resourceClass; Kate minimized (keeps its running dot).
const layout = {
    "org.kde.kate": {x: 360, y: 60, width: 1200, height: 700, minimized: true},
    "org.kde.dolphin": {x: 170, y: 110, width: 1000, height: 620},
    "org.kde.konsole": {x: 1010, y: 360, width: 760, height: 470},
};
const order = ["org.kde.kate", "org.kde.dolphin", "org.kde.konsole"];
for (const cls of order) {
    for (const w of workspace.windowList()) {
        if (w.resourceClass !== cls || !w.normalWindow) continue;
        const g = layout[cls];
        w.minimized = false;
        w.frameGeometry = {x: g.x, y: g.y, width: g.width, height: g.height};
        workspace.activeWindow = w;
        if (g.minimized) w.minimized = true;
    }
}
