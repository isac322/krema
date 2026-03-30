// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

FormCard.FormCardPage {
    title: i18n("Window Preview")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

    FormSection {
        FormCard.FormSwitchDelegate {
            text: i18n("Enable window preview")
            description: i18n("Show window thumbnails when hovering dock items")
            checked: DockSettings.previewEnabled
            onToggled: DockSettings.previewEnabled = checked
        }
    }

    // --- Sample Preview Mockup ---
    FormCard.FormHeader {
        title: i18n("Preview Appearance")
        visible: DockSettings.previewEnabled
    }

    FormSection {
        visible: DockSettings.previewEnabled

        FormCard.AbstractFormDelegate {
            background: null
            contentItem: Item {
                implicitHeight: previewMockup.height + Kirigami.Units.largeSpacing * 2

                // Preview popup mockup
                Rectangle {
                    id: previewMockup
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    width: DockSettings.previewThumbnailSize + Kirigami.Units.largeSpacing * 2
                    height: thumbnailArea.height + titleRow.height + Kirigami.Units.largeSpacing * 3
                    radius: Kirigami.Units.largeSpacing
                    color: Kirigami.Theme.backgroundColor

                    Behavior on width {
                        NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        // App title row
                        RowLayout {
                            id: titleRow
                            Layout.fillWidth: true
                            Kirigami.Icon {
                                source: "internet-web-browser"
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                            }
                            QQC2.Label {
                                text: "Sample Window"
                                font.bold: true
                                Layout.fillWidth: true
                            }
                            Kirigami.Icon {
                                source: "window-close"
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                                opacity: 0.5
                            }
                        }

                        // Thumbnail placeholder
                        Rectangle {
                            id: thumbnailArea
                            Layout.fillWidth: true
                            Layout.preferredHeight: width * 0.6
                            radius: Kirigami.Units.smallSpacing
                            color: Kirigami.Theme.alternateBackgroundColor

                            // Fake window content
                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 4
                                Rectangle { width: parent.parent.width * 0.6; height: 4; radius: 2; color: Kirigami.Theme.disabledTextColor; opacity: 0.3; Layout.alignment: Qt.AlignHCenter }
                                Rectangle { width: parent.parent.width * 0.8; height: 4; radius: 2; color: Kirigami.Theme.disabledTextColor; opacity: 0.2; Layout.alignment: Qt.AlignHCenter }
                                Rectangle { width: parent.parent.width * 0.5; height: 4; radius: 2; color: Kirigami.Theme.disabledTextColor; opacity: 0.2; Layout.alignment: Qt.AlignHCenter }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Settings ---
    FormCard.FormHeader {
        title: i18n("Settings")
        visible: DockSettings.previewEnabled
    }

    FormSection {
        visible: DockSettings.previewEnabled

        FormCard.AbstractFormDelegate {
            Accessible.name: i18n("Thumbnail width")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label { Layout.fillWidth: true; text: i18n("Thumbnail width") }
                    QQC2.Label { text: thumbSlider.value + "px"; color: Kirigami.Theme.disabledTextColor }
                }
                QQC2.Slider {
                    id: thumbSlider
                    Layout.fillWidth: true
                    from: 120; to: 320; stepSize: 20
                    value: DockSettings.previewThumbnailSize
                    onMoved: DockSettings.previewThumbnailSize = value
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        FormCard.AbstractFormDelegate {
            Accessible.name: i18n("Hover delay")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label { Layout.fillWidth: true; text: i18n("Hover delay") }
                    QQC2.Label { text: hoverDelaySlider.value + "ms"; color: Kirigami.Theme.disabledTextColor }
                }
                QQC2.Slider {
                    id: hoverDelaySlider
                    Layout.fillWidth: true
                    from: 0; to: 2000; stepSize: 50
                    value: DockSettings.previewHoverDelay
                    onMoved: DockSettings.previewHoverDelay = value
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        FormCard.AbstractFormDelegate {
            Accessible.name: i18n("Hide delay")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label { Layout.fillWidth: true; text: i18n("Hide delay") }
                    QQC2.Label { text: hideDelaySlider.value + "ms"; color: Kirigami.Theme.disabledTextColor }
                }
                QQC2.Slider {
                    id: hideDelaySlider
                    Layout.fillWidth: true
                    from: 0; to: 1000; stepSize: 50
                    value: DockSettings.previewHideDelay
                    onMoved: DockSettings.previewHideDelay = value
                }
            }
        }
    }
}
