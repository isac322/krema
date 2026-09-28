// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Test-only helper: resets every mock singleton. Call from TestCase.init().
pragma Singleton
import QtQuick

QtObject {
    function resetAll() {
        DockSettings.reset()
        DockView.reset()
        DockVisibility.reset()
        DockActions.reset()
        DockContextMenu.reset()
        PreviewController.reset()
        NotificationTracker.reset()
        LauncherEntryTracker.reset()
        DockModel.reset()
    }
}
