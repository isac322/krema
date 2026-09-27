// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Mock of the Unity LauncherEntry backend (plasma-workspace SmartLauncherItem).
// Defaults to "no data" so DockItem falls back to NotificationTracker.
import QtQuick

QtObject {
    property url launcherUrl
    property int count: 0
    property bool countVisible: false
    property bool urgent: false
    property int progress: 0
    property bool progressVisible: false
}
