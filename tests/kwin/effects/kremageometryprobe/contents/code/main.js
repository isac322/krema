// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Read the compositor's actual animation target, not the task model's data.
// Show Desktop supplies a D-Bus-triggerable snapshot signal without depending
// on a geometry-changed signal (iconGeometry has no such effect API signal).
let snapshot = 0;
effects.showingDesktopChanged.connect(function () {
    const windows = effects.stackingOrder;
    const records = [];
    for (let i = 0; i < windows.length; i++) {
        const w = windows[i];
        if (w.deleted) {
            continue;
        }
        const icon = w.iconGeometry;
        records.push({
            caption: w.caption,
            dock: w.dock,
            normal: w.normalWindow,
            managed: w.managed,
            frame: { x: w.x, y: w.y, width: w.width, height: w.height },
            icon: { x: icon.x, y: icon.y, width: icon.width, height: icon.height }
        });
    }
    console.info("KREMA_GEOMETRY " + JSON.stringify({ sequence: ++snapshot, windows: records }));
});
