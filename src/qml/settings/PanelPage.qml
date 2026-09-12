// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0

// Import our custom UI Kit
import "../components"

QQC2.ScrollView {
    id: panelPage
    contentWidth: availableWidth
    clip: true

    topPadding: 16
    bottomPadding: 32
    leftPadding: 16
    rightPadding: 16

    ColumnLayout {
        id: panelLayout
        width: parent.width
        spacing: 32

        // Rule 6 Helpers: Mathematical Constitution for the Panel Ceiling
        readonly property real _floorPadding: Math.max(4, Math.round(DockSettings.iconSize * 0.25))
        readonly property real _dotHeight: Math.max(2, Math.round(DockSettings.iconSize * 0.10))
        readonly property real _indicatorGap: Math.max(2, Math.round(DockSettings.iconSize * 0.125) + Math.round(DockSettings.iconSize * 0.15 * (1.0 - DockSettings.indicatorOffset)))
        readonly property real _totalFloorUnit: _floorPadding + _dotHeight + _indicatorGap

        // Glass Pill envelope calculator (used by sliders)
        function calculateMaxEnv(size) {
            let floor = Math.max(4, Math.round(size * 0.25))
            let ind = Math.max(2, Math.round(size * 0.10))
            let gap = Math.max(2, Math.round(size * 0.125) + Math.round(size * 0.15 * (1.0 - DockSettings.indicatorOffset)))
            return size + floor + ind + gap + floor
        }

        // --- SECTION 1: PHYSICAL GEOMETRY ---
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            
            QQC2.Label { 
                text: i18n("Geometry & Structure")
                color: theme.textDim
                font.bold: true; font.letterSpacing: 1.1; font.pixelSize: 12
                Layout.leftMargin: 8
            }
            
            KremaCard {
                id: panelCard
                // PANEL SIZE (Proportional Scaling)
                ColumnLayout {
                    Layout.fillWidth: true
                    
                    // Ratio tracking: prevents the slider bounds from shifting while actively dragging
                    property real capturedRatio: 0
                    property real stableRatio: DockSettings.iconSize / DockSettings.panelHeight
                    property real activeRatio: masterZoomSlider.pressed ? capturedRatio : stableRatio
                    
                    RowLayout {
                        QQC2.Label { Layout.fillWidth: true; text: i18n("Panel Size"); color: theme.text; font.bold: true }
                        QQC2.Label { text: masterZoomSlider.value + "px"; color: theme.textDim; font.bold: true }
                    }
                    QQC2.Slider {
                        id: masterZoomSlider; Layout.fillWidth: true; 
                        
                        // dynamic ceiling: either 140px, or the panel height that hits 96px icons
                        to: Math.min(140, Math.round(96 / parent.activeRatio))
                        
                        // dynamic floor: either 20px (or glass pill floor if no overflow), or the panel height that hits 12px icons
                        from: {
                            let lowestIconFloor = Math.round(12 / parent.activeRatio);
                            let structuralFloor = DockSettings.allowOverflow ? 20 : panelLayout.calculateMaxEnv(12);
                            return Math.max(structuralFloor, lowestIconFloor);
                        }
                        stepSize: 2; 
                        value: DockSettings.panelHeight;
                        
                        onPressedChanged: {
                            if (pressed) {
                                parent.capturedRatio = parent.stableRatio;
                            } else {
                                DockSettings.save();
                            }
                        }
                        
                        onMoved: {
                            let newPanelHeight = value;
                            let newIconSize = Math.round(parent.capturedRatio * newPanelHeight);
                            
                            // Clamp iconSize to valid range
                            if (newIconSize > 96) {
                                newIconSize = 96;
                            } else if (newIconSize < 12) {
                                newIconSize = 12;
                            }
                            
                            // Since from/to already encapsulate the structural and icon limits, 
                            // we just apply them directly!
                            DockSettings.iconSize = newIconSize;
                            DockSettings.panelHeight = newPanelHeight;
                        }
                    }
                    QQC2.Label { 
                        text: i18n("Scales the entire dock (Panel + Glass Pill) proportionally."); 
                        color: theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 4
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

                // PANEL THICKNESS (FINE-TUNING)
                ColumnLayout {
                    Layout.fillWidth: true
                    
                    RowLayout {
                        QQC2.Label { Layout.fillWidth: true; text: i18n("Panel Thickness (Outer Edge)"); color: theme.text; font.bold: true }
                        QQC2.Label { text: thicknessSlider.value + "px"; color: theme.textDim; font.bold: true }
                    }
                    QQC2.Slider {
                        id: thicknessSlider; Layout.fillWidth: true; 
                        // Dynamic floor: 20px when overflow allowed, glass pill height when not.
                        from: DockSettings.allowOverflow ? 20 : Math.floor(panelLayout.calculateMaxEnv(DockSettings.iconSize)); to: 140; stepSize: 2; 
                        value: DockSettings.panelHeight; 
                        onMoved: {
                            DockSettings.panelHeight = value;
                        }
                        onPressedChanged: if (!pressed) DockSettings.save()
                    }
                    QQC2.Label { 
                        text: i18n("Strictly grows/shrinks the outer dark panel without scaling the Glass Pill inside."); 
                        color: theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 4
                    }
                    
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A"; Layout.topMargin: 4; Layout.bottomMargin: 4 }
                    
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            QQC2.Label { text: i18n("Allow Visual Overflow"); color: theme.text; font.bold: true }
                            QQC2.Label { 
                                text: i18n("When enabled, the panel can shrink below the Glass Pill, letting icons protrude above the panel edge."); 
                                color: theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
                            }
                        }
                        QQC2.Switch {
                            checked: DockSettings.allowOverflow
                            onToggled: {
                                DockSettings.allowOverflow = checked;
                                DockSettings.save();
                                // If turning OFF and panel is currently overflowing, snap to glass pill floor
                                if (!checked) {
                                    let floor = Math.floor(panelLayout.calculateMaxEnv(DockSettings.iconSize));
                                    if (DockSettings.panelHeight < floor) {
                                        DockSettings.panelHeight = floor;
                                        DockSettings.save();
                                    }
                                }
                            }
                        }
                    }
                }




                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

                // MAXIMUM LENGTH
                ColumnLayout {
                    Layout.fillWidth: true
                    RowLayout {
                        QQC2.Label { Layout.fillWidth: true; text: i18n("Maximum Dock Length"); color: theme.text; font.bold: true }
                        QQC2.Label { text: maxLengthSlider.value + "%"; color: theme.textDim; font.bold: true }
                    }
                    QQC2.Slider {
                        id: maxLengthSlider; Layout.fillWidth: true; 
                        from: 10; to: DockSettings.floating ? 99 : 100; stepSize: 1; 
                        value: DockSettings.maxLength; 
                        onMoved: DockSettings.maxLength = value
                        onPressedChanged: if (!pressed) DockSettings.save()
                        onToChanged: if (value > to) DockSettings.maxLength = to
                    }
                    QQC2.Label { 
                        text: i18n("In Adaptive mode, this is the maximum allowed width. In Span mode, this dictates the exact panel width (e.g. 100% = full screen)."); 
                        color: theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

                // PANEL LENGTH MODE
                ColumnLayout {
                    Layout.fillWidth: true
                    QQC2.Label { Layout.fillWidth: true; text: i18n("Panel Length Mode"); color: theme.text; font.bold: true }
                    RowLayout {
                        Layout.fillWidth: true
                        QQC2.RadioButton {
                            text: i18n("Adaptive (Hugs Icons)")
                            checked: DockSettings.panelLengthMode === 0
                            onToggled: if (checked) { DockSettings.panelLengthMode = 0; DockSettings.save() }
                        }
                        QQC2.RadioButton {
                            text: i18n("Span Screen (Fixed Width)")
                            checked: DockSettings.panelLengthMode === 1
                            onToggled: if (checked) { DockSettings.panelLengthMode = 1; DockSettings.save() }
                        }
                    }
                    QQC2.Label { 
                        text: i18n("Adaptive mode shrinks the panel background to wrap your icons. Span mode physically stretches the panel background across the screen like a standard taskbar."); 
                        color: theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

                // CORNER RADIUS
                ColumnLayout {
                    Layout.fillWidth: true
                    RowLayout {
                        QQC2.Label { Layout.fillWidth: true; text: i18n("Corner Radius"); color: theme.text; font.bold: true }
                        QQC2.Label { text: radiusSlider.value + "px"; color: theme.textDim; font.bold: true }
                    }
                    QQC2.Slider {
                        id: radiusSlider; Layout.fillWidth: true; 
                        // Rule 6: UI Blindness Prevention. Max radius is mathematically capped at 
                        // half of the panel's thickness to prevent 'dead zones' and rendering glitches.
                        from: 0; to: Math.floor(thicknessSlider.value / 2); stepSize: 1; 
                        value: DockSettings.cornerRadius; 
                        onMoved: DockSettings.cornerRadius = value
                        onPressedChanged: if (!pressed) DockSettings.save()
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

                // FLOATING SWITCH
                KremaSwitch {
                    Layout.fillWidth: true
                    text: i18n("Floating Dock")
                    checked: DockSettings.floating
                    onToggled: DockSettings.floating = checked
                }
                QQC2.Label { 
                    text: i18n("Detaches the dock from the screen edge for a modern, pill-shaped look."); 
                    color: theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
            }
        }
    }
}
