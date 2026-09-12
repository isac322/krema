// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Import our custom UI Kit
import "../components"
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import com.bhyoo.krema 1.0
import org.kde.kirigami as Kirigami

QQC2.ScrollView {
    id: previewPage

    contentWidth: availableWidth
    clip: true
    topPadding: 16
    bottomPadding: 32
    leftPadding: 16
    rightPadding: 16

    ColumnLayout {
        width: parent.width
        spacing: 32

        // --- SECTION 1: ACTIVATION ---
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            QQC2.Label {
                text: i18n("Master Switch")
                color: theme.textDim
                font.bold: true
                font.letterSpacing: 1.1
                font.pixelSize: 12
                Layout.leftMargin: 8
            }

            KremaCard {
                KremaSwitch {
                    Layout.fillWidth: true
                    text: i18n("Enable Window Previews")
                    checked: DockSettings.previewEnabled
                    onToggled: {
                        DockSettings.previewEnabled = checked;
                        DockSettings.save();
                    }
                }

                QQC2.Label {
                    text: i18n("Shows a live thumbnail of open windows when hovering over dock icons.")
                    color: theme.textDim
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.topMargin: -8
                }

            }

        }

        // --- SECTION 2: DIMENSIONS & TIMING ---
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: DockSettings.previewEnabled

            QQC2.Label {
                text: i18n("Dimensions & Timing")
                color: theme.textDim
                font.bold: true
                font.letterSpacing: 1.1
                font.pixelSize: 12
                Layout.leftMargin: 8
            }

            KremaCard {
                // THUMBNAIL WIDTH
                ColumnLayout {
                    Layout.fillWidth: true

                    RowLayout {
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: i18n("Thumbnail Width")
                            color: theme.text
                            font.bold: true
                        }

                        QQC2.Label {
                            text: widthSlider.value + "px"
                            color: theme.textDim
                            font.bold: true
                        }

                    }

                    QQC2.Slider {
                        id: widthSlider

                        Layout.fillWidth: true
                        from: 120
                        to: 320
                        stepSize: 20
                        value: DockSettings.previewThumbnailSize
                        onMoved: {
                            DockSettings.previewThumbnailSize = value;
                            DockSettings.save();
                        }
                    }

                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#2A282A"
                }

                // SHOW DELAY
                ColumnLayout {
                    Layout.fillWidth: true

                    RowLayout {
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: i18n("Show Delay (Hover)")
                            color: theme.text
                            font.bold: true
                        }

                        QQC2.Label {
                            text: hoverSlider.value + "ms"
                            color: theme.textDim
                            font.bold: true
                        }

                    }

                    QQC2.Slider {
                        id: hoverSlider

                        Layout.fillWidth: true
                        from: 0
                        to: 2000
                        stepSize: 50
                        value: DockSettings.previewHoverDelay
                        onMoved: {
                            DockSettings.previewHoverDelay = value;
                            DockSettings.save();
                            KremaDebug.model("Settings: previewHoverDelay changed to " + value + "ms");
                        }
                    }

                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#2A282A"
                }

                // HIDE DELAY
                ColumnLayout {
                    Layout.fillWidth: true

                    RowLayout {
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: i18n("Hide Delay (Exit)")
                            color: theme.text
                            font.bold: true
                        }

                        QQC2.Label {
                            text: hideSlider.value + "ms"
                            color: theme.textDim
                            font.bold: true
                        }

                    }

                    QQC2.Slider {
                        id: hideSlider

                        Layout.fillWidth: true
                        from: 0
                        to: 1000
                        stepSize: 50
                        value: DockSettings.previewHideDelay
                        onMoved: {
                            DockSettings.previewHideDelay = value;
                            DockSettings.save();
                            if (KremaDebug.modelEnabled)
                                KremaDebug.model("Settings: previewHideDelay changed to " + value + "ms");

                        }
                    }

                }

            }

        }

    }

}
