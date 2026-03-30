// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

FormCard.FormCardPage {
    title: i18n("Icons")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

    // --- Mini Dock Preview ---
    FormCard.FormCard {
        id: previewCard

        FormCard.AbstractFormDelegate {
            background: null
            contentItem: Item {
                implicitHeight: dockPreviewContainer.height + Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.largeSpacing * 2

                // Dock preview container
                Rectangle {
                    id: dockPreviewContainer
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    width: previewRow.implicitWidth + Kirigami.Units.largeSpacing * 2
                    height: previewRow.implicitHeight + Kirigami.Units.largeSpacing * 2
                    radius: Kirigami.Units.largeSpacing
                    color: Kirigami.Theme.alternateBackgroundColor

                    Row {
                        id: previewRow
                        anchors.centerIn: parent
                        spacing: DockSettings.iconSpacing

                        // Icons chosen for: (1) guaranteed on KDE (Breeze), (2) mix of
                        // padding-heavy vs padding-free to demonstrate normalization.
                        // "application-x-executable" and "preferences-desktop-theme" have
                        // visible internal padding; "folder" and "utilities-terminal" fill
                        // the canvas, making normalization differences obvious.
                        property var iconNames: [
                            "system-file-manager",
                            "utilities-terminal",
                            "application-x-executable",
                            "preferences-system",
                            "internet-web-browser",
                            "accessories-text-editor",
                            "preferences-desktop-theme"
                        ]
                        property bool showStaticHover: true
                        property int hoveredIdx: showStaticHover ? 3 : -1

                        Repeater {
                            model: previewRow.iconNames

                            // Each icon sits inside a fixed-size cell.
                            // Normalization simulation: icons with internal padding
                            // (indices 2, 6) are rendered smaller when normalization
                            // is OFF, and fill the cell when ON — mimicking the real
                            // dock's TaskIconProvider padding-removal behavior.
                            Item {
                                required property string modelData
                                required property int index

                                // Cell size = iconSize (same for all icons)
                                width: DockSettings.iconSize
                                height: DockSettings.iconSize

                                // Simulated padding ratios: icons known to have
                                // internal padding get a penalty when normalization is OFF.
                                // 1.0 = no padding, 0.78 = 22% padding.
                                property real simulatedPadding: {
                                    if (DockSettings.iconNormalization) return DockSettings.iconScale
                                    // OFF: padding-heavy icons shrink
                                    switch (index) {
                                    case 2: return 0.78  // application-x-executable
                                    case 6: return 0.82  // preferences-desktop-theme
                                    case 3: return 0.90  // preferences-system
                                    default: return 1.0
                                    }
                                }

                                Kirigami.Icon {
                                    source: parent.modelData
                                    width: DockSettings.iconSize * parent.simulatedPadding
                                    height: DockSettings.iconSize * parent.simulatedPadding
                                    anchors.centerIn: parent
                                }

                                // Parabolic zoom on the cell (same formula as the real dock)
                                scale: {
                                    let zoomRange = DockSettings.iconSize * 2.5
                                    let maxZoom = DockSettings.maxZoomFactor

                                    if (previewHoverArea.containsMouse) {
                                        let center = x + width / 2
                                        let mouse = previewHoverArea.mouseX - previewRow.x
                                        let dist = Math.abs(mouse - center)
                                        if (dist < zoomRange) {
                                            return 1.0 + (maxZoom - 1.0) * (1.0 - dist / zoomRange)
                                        }
                                        return 1.0
                                    }

                                    // Static parabolic: no zoom when hover preview is off
                                    if (previewRow.hoveredIdx < 0) return 1.0
                                    let step = DockSettings.iconSize + DockSettings.iconSpacing
                                    let dist = Math.abs(index - previewRow.hoveredIdx) * step
                                    if (dist < zoomRange) {
                                        return 1.0 + (maxZoom - 1.0) * (1.0 - dist / zoomRange)
                                    }
                                    return 1.0
                                }
                                transformOrigin: Item.Bottom

                                Behavior on scale {
                                    NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                                }

                            }
                        }
                    }

                    MouseArea {
                        id: previewHoverArea
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }

                // Toggle: show/hide static hover zoom in preview
                RowLayout {
                    anchors.right: dockPreviewContainer.right
                    anchors.top: dockPreviewContainer.bottom
                    anchors.topMargin: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        text: i18n("Hover")
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        color: Kirigami.Theme.disabledTextColor
                    }
                    QQC2.Switch {
                        checked: previewRow.showStaticHover
                        onToggled: previewRow.showStaticHover = checked
                    }
                }
            }
        }
    }

    // --- Size & Spacing ---
    FormCard.FormHeader {
        title: i18n("Size & Spacing")
    }

    FormSection {
        FormCard.AbstractFormDelegate {
            id: iconSizeDelegate
            Accessible.name: i18n("Icon size")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Icon size")
                        color: iconSizeDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    }
                    QQC2.Label {
                        text: iconSizeSlider.value + "px"
                        color: Kirigami.Theme.disabledTextColor
                    }
                }
                QQC2.Slider {
                    id: iconSizeSlider
                    Layout.fillWidth: true
                    from: 24; to: 96; stepSize: 4
                    value: DockSettings.iconSize
                    onMoved: DockSettings.iconSize = value
                    Accessible.name: i18n("Icon size")
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        FormCard.AbstractFormDelegate {
            id: spacingDelegate
            Accessible.name: i18n("Icon spacing")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Icon spacing")
                        color: spacingDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    }
                    QQC2.Label {
                        text: spacingSlider.value + "px"
                        color: Kirigami.Theme.disabledTextColor
                    }
                }
                QQC2.Slider {
                    id: spacingSlider
                    Layout.fillWidth: true
                    from: 0; to: 16; stepSize: 1
                    value: DockSettings.iconSpacing
                    onMoved: DockSettings.iconSpacing = value
                    Accessible.name: i18n("Icon spacing")
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        FormCard.AbstractFormDelegate {
            id: zoomDelegate
            Accessible.name: i18n("Zoom factor")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Zoom factor")
                        color: zoomDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    }
                    QQC2.Label {
                        text: zoomSlider.value.toFixed(1) + "x"
                        color: Kirigami.Theme.disabledTextColor
                    }
                }
                QQC2.Slider {
                    id: zoomSlider
                    Layout.fillWidth: true
                    from: 1.0; to: 2.0; stepSize: 0.1
                    value: DockSettings.maxZoomFactor
                    onMoved: DockSettings.maxZoomFactor = value
                    Accessible.name: i18n("Zoom factor")
                }
            }
        }
    }

    // --- Appearance ---
    FormCard.FormHeader {
        title: i18n("Appearance")
    }

    FormSection {
        FormCard.FormSwitchDelegate {
            text: i18n("Icon size normalization")
            description: i18n("Automatically adjust icons with excess padding to appear visually consistent")
            checked: DockSettings.iconNormalization
            onToggled: DockSettings.iconNormalization = checked
        }

        FormCard.FormDelegateSeparator {}

        FormCard.AbstractFormDelegate {
            id: iconScaleDelegate
            Accessible.name: i18n("Icon scale")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Icon scale")
                        color: iconScaleDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    }
                    QQC2.Label {
                        text: Math.round(iconScaleSlider.value * 100) + "%"
                        color: Kirigami.Theme.disabledTextColor
                    }
                }
                QQC2.Slider {
                    id: iconScaleSlider
                    Layout.fillWidth: true
                    from: 0.5; to: 1.0; stepSize: 0.05
                    value: DockSettings.iconScale
                    onMoved: DockSettings.iconScale = value
                    Accessible.name: i18n("Icon scale")
                }
            }
        }
    }
}
