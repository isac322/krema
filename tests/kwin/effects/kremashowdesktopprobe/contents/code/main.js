// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Test-only KWin effect for test_show_desktop.cpp. On every Show Desktop
// toggle it logs KWin's own classification of each window, including
// hiddenByShowDesktop, which only the effect API exposes, and the window-type
// flags Slide Back uses to pick its usable windows (normal, dialog, keepAbove,
// minimized). The test reads the lines back from the KWin log
// (run-with-kwin.sh exports its path).
let toggle = 0;
effects.showingDesktopChanged.connect(function (showing) {
    toggle++;
    const windows = effects.stackingOrder;
    for (let i = 0; i < windows.length; i++) {
        const w = windows[i];
        console.info("KREMA_SHOWDESKTOP toggle=" + toggle
                     + " showing=" + showing
                     + " class=" + w.windowClass.replace(/ /g, "/")
                     + " dock=" + w.dock
                     + " deleted=" + w.deleted
                     + " hiddenByShowDesktop=" + w.hiddenByShowDesktop
                     + " normal=" + w.normalWindow
                     + " dialog=" + w.dialog
                     + " keepAbove=" + w.keepAbove
                     + " minimized=" + w.minimized
                     + " size=" + Math.round(w.width) + "x" + Math.round(w.height));
    }
});
