// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0

/**
 * @brief Tier 1: Background Panel.
 * Implements styling (Adaptive, Tinted, Acrylic, Mica)
 */
Rectangle {
    id: root
    
    // Default properties that can be bound from MainDock
    property int backgroundStyle: DockView.screenSettings.backgroundStyle
    property real backgroundOpacity: DockView.screenSettings.backgroundOpacity
    property color activeTintColor: (backgroundStyle === 2 && !DockSettings.useSystemColor) ? Qt.color(DockSettings.tintColor) : DockView.backgroundColor
    property bool rimLightEnabled: DockSettings.rimLightEnabled
    property real rimLightOpacity: DockSettings.rimLightOpacity

    color: {
        if (backgroundStyle === 3 || backgroundStyle === 4) return "transparent" // Acrylic or Mica
        if (backgroundStyle === 1) return "transparent" // Completely Transparent
        return Qt.rgba(activeTintColor.r, activeTintColor.g, activeTintColor.b, backgroundOpacity)
    }
    
    border.color: rimLightEnabled ? Qt.rgba(1, 1, 1, rimLightOpacity) : "transparent"
    border.width: rimLightEnabled ? 1 : 0

    ShaderEffect {
        id: acrylicShader
        anchors.fill: parent
        z: 0
        visible: root.backgroundStyle === 3 || root.backgroundStyle === 4

        property color _activeTint: {
            if (root.backgroundStyle === 4) return Kirigami.Theme.highlightColor // Mica uses accent
            return root.activeTintColor
        }
        property real tintR: _activeTint.r
        property real tintG: _activeTint.g
        property real tintB: _activeTint.b
        property real tintOpacity: root.backgroundStyle === 4 ? 0.3 : root.backgroundOpacity
        property real noiseStrength: root.backgroundStyle === 4 ? 0.05 : 0.02
        property real resX: root.width
        property real resY: root.height
        property real cornerRadius: root.radius

        fragmentShader: "qrc:/qml/shaders/acrylic_overlay.frag.qsb"
    }
}
