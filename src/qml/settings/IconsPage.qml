// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0
import ".."

SettingsPage {
    id: page
    title: i18n("Icons")
    subtitle: i18n("Size, spacing, hover zoom and opacity of the dock icons")

    readonly property ZoomAnimationProfile zoomProfile: ZoomAnimationProfile {}
    readonly property bool zoomCustom: DockSettings.zoomAnimationPreset === zoomProfile.customPreset
    // Last non-custom preset chosen in this page session; the Preset tab restores it.
    // Seeded once (not bound) so switching to Custom does not reset it.
    property int lastZoomPreset: 0

    readonly property bool zoomEnabled: DockSettings.maxZoomFactor > 1.0
    // The stage's middle icon is shown magnified until the pointer hovers it.
    property bool showStaticHover: true

    Component.onCompleted: {
        const preset = DockSettings.zoomAnimationPreset
        if (preset >= 0 && preset < zoomProfile.customPreset) {
            lastZoomPreset = preset
        }
    }

    function selectZoomPreset(preset) {
        lastZoomPreset = preset
        DockSettings.zoomAnimationPreset = preset
    }

    function zoomTimingSummary(preset) {
        const timing = zoomProfile.presets[preset]
        return i18n("Zoom in %1 ms, zoom out %2 ms", timing.inDuration, timing.outDuration)
    }

    // Same scaling as ZoomAnimationProfile.effectiveZoom*Duration, so the
    // previews play at the speed the dock will use.
    function effectiveDuration(ms) {
        return Math.round(ms * Kirigami.Units.shortDuration / 100.0)
    }

    readonly property var zoomEasingNames: [
        i18n("Linear"),
        i18n("Ease in"),
        i18n("Ease out"),
        i18n("Ease in and out"),
        i18n("Gentle ease in and out")
    ]

    readonly property var zoomPresetChoices: [
        {
            text: i18n("Natural"),
            description: i18n("Eases in and out, similar to the macOS Dock (default)") + "\n" + zoomTimingSummary(0)
        },
        {
            text: i18n("Quick"),
            description: i18n("Fast ease-out, the previous Krema default") + "\n" + zoomTimingSummary(1)
        },
        {
            text: i18n("Relaxed"),
            description: i18n("Slow, gentle magnification") + "\n" + zoomTimingSummary(2)
        },
        {
            text: i18n("Instant"),
            description: i18n("No zoom transition")
        }
    ]

    // --- Inline preview components ---

    // Picker thumbnail: the desktop cropped around a dock sized to fit the
    // tile. Configure the dock through `dock`.
    component DockTile: DesktopStage {
        id: tile

        property alias dock: tileDock

        anchors.fill: parent
        elevated: false
        radius: 0
        active: page.windowActive
        // Fit the resting dock along the edge and the zoomed icons across it.
        unit: Math.min(0.85 * (tileDock.vertical ? height : width) / tileDock.restLengthReal,
                       0.7 * (tileDock.vertical ? width : height)
                           / (tileDock.iconSize * Math.max(1.0, tileDock.maxZoomFactor) + 2 * Kirigami.Units.largeSpacing + 8))
        Accessible.ignored: true

        MiniDock {
            id: tileDock
            unit: tile.unit
            edge: tile.edge
            backdrop: tile.backdrop
            active: tile.active
        }
    }

    // Zoom amount over time for a zoom-in, a short hold and a zoom-out.
    // Durations are settings milliseconds; easings are easing indices
    // (ZoomAnimationProfile order).
    component ZoomCurve: Item {
        id: curve

        property int inDuration
        property int outDuration
        property int inEasing
        property int outEasing

        readonly property real holdDuration: 200
        readonly property real margin: Kirigami.Units.smallSpacing

        function ease(index, t) {
            switch (index) {
            case 0: return t
            case 1: return t * t * t
            case 2: return 1 - Math.pow(1 - t, 3)
            case 4: return -(Math.cos(Math.PI * t) - 1) / 2
            default: return t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2
            }
        }

        readonly property var points: {
            const w = width - margin * 2
            const h = height - margin * 2
            if (w <= 0 || h <= 0) {
                return []
            }
            const total = Math.max(1, inDuration + holdDuration + outDuration)
            const xAt = ms => margin + w * ms / total
            const yAt = amount => margin + h * (1 - amount)
            const samples = 24
            const result = []
            for (let i = 0; i <= samples; ++i) {
                const t = i / samples
                result.push(Qt.point(xAt(inDuration * t), yAt(ease(inEasing, t))))
            }
            const outStart = inDuration + holdDuration
            for (let i = 0; i <= samples; ++i) {
                const t = i / samples
                result.push(Qt.point(xAt(outStart + outDuration * t), yAt(1 - ease(outEasing, t))))
            }
            return result
        }

        // Baseline (rest size) and top line (full zoom).
        Rectangle {
            x: curve.margin
            y: curve.height - curve.margin
            width: curve.width - curve.margin * 2
            height: 1
            color: page.secondaryTextColor
        }
        Rectangle {
            x: curve.margin
            y: curve.margin
            width: curve.width - curve.margin * 2
            height: 1
            color: Qt.alpha(page.secondaryTextColor, 0.4)
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: Kirigami.Theme.highlightColor
                strokeWidth: 2
                fillColor: Qt.alpha(Kirigami.Theme.highlightColor, 0)
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin

                PathPolyline {
                    path: curve.points
                }
            }
        }
    }

    // A padded icon on a small tile, for the normalization comparison.
    component NormalizationSample: ColumnLayout {
        id: sample

        property real fill
        property string label
        property bool current

        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: Kirigami.Units.gridUnit * 2.5
            implicitHeight: implicitWidth
            radius: Kirigami.Units.cornerRadius * 2
            color: Kirigami.Theme.alternateBackgroundColor
            border.width: sample.current ? 2 : 0
            border.color: Kirigami.Theme.highlightColor

            Kirigami.Icon {
                anchors.centerIn: parent
                width: (parent.width - Kirigami.Units.smallSpacing * 2) * sample.fill
                height: width
                source: "org.kde.kate"
                fallback: "accessories-text-editor"
            }
        }

        QQC2.Label {
            Layout.alignment: Qt.AlignHCenter
            text: sample.label
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            font.bold: sample.current
            color: sample.current ? Kirigami.Theme.textColor : page.secondaryTextColor
        }
    }

    // --- Stage: the desktop with a live dock ---

    stage: Component {
        Item {
            implicitHeight: stage.implicitHeight

            DesktopStage {
                id: stage

                readonly property real coverUnit: Math.max(width / screenSize.width, height / screenSize.height)

                anchors.fill: parent
                active: page.windowActive
                // Default framing around the dock, shrunk when a large dock
                // would not fit along its edge.
                unit: Math.max(coverUnit, Math.min(framingUnit,
                    0.85 * (dock.vertical ? height : width) / dock.restLengthReal))

                Accessible.role: Accessible.Graphic
                Accessible.name: i18n("Dock preview")
                Accessible.description: i18n("Sample dock showing the icon size, spacing, zoom, zoom animation, normalization, scale and icon opacities")

                // Measurement overlay for the geometry sliders.
                measureOrientation: dock.vertical ? Qt.Vertical : Qt.Horizontal
                measureTarget: iconSizeSlider.active ? dock.focusIcon
                    : iconSpacingSlider.active ? dock.spacingMarker
                    : zoomFactorSlider.active ? dock.focusIcon
                    : null
                measureText: iconSpacingSlider.active
                    ? i18nc("@label pixels", "%1 px", DockSettings.iconSpacing)
                    : zoomFactorSlider.active
                        ? i18nc("@label zoom factor and zoomed icon size", "%1x · %2 px",
                                DockSettings.maxZoomFactor.toFixed(1),
                                Math.round(DockSettings.iconSize * DockSettings.maxZoomFactor))
                        : i18nc("@label pixels", "%1 px", DockSettings.iconSize)

                MiniDock {
                    id: dock
                    unit: stage.unit
                    backdrop: stage.backdrop
                    active: stage.active
                    // Zoomed while the zoom factor is adjusted, at rest while
                    // size or spacing are measured.
                    hoveredIndex: zoomFactorSlider.active ? middleIndex
                        : (iconSizeSlider.active || iconSpacingSlider.active) ? -1
                        : page.showStaticHover ? middleIndex : -1
                }
            }

            // Toggle: show/hide the static hover zoom on the stage, as a pill
            // in the stage's top corner.
            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: Kirigami.Units.largeSpacing
                implicitWidth: hoverToggleRow.implicitWidth + Kirigami.Units.largeSpacing * 2
                implicitHeight: hoverToggleRow.implicitHeight + Kirigami.Units.smallSpacing
                radius: height / 2
                Kirigami.Theme.colorSet: Kirigami.Theme.Window
                Kirigami.Theme.inherit: false
                color: Qt.alpha(Kirigami.Theme.backgroundColor, 0.92)

                RowLayout {
                    id: hoverToggleRow
                    anchors.centerIn: parent
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        text: i18n("Hover")
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        color: Kirigami.Theme.textColor
                        Accessible.ignored: true
                    }
                    QQC2.Switch {
                        checked: page.showStaticHover
                        onToggled: page.showStaticHover = checked
                        Accessible.name: i18n("Show hover zoom in preview")
                    }
                }
            }
        }
    }

    // --- Size & Spacing ---
    FormCard.FormHeader {
        title: i18n("Size & Spacing")
    }

    FormCard.FormCard {
        SliderDelegate {
            id: iconSizeSlider
            text: i18n("Icon size")
            from: 24; to: 96; stepSize: 4
            value: DockSettings.iconSize
            valueText: i18nc("@label pixels", "%1 px", value)
            onMoved: (value) => DockSettings.iconSize = value
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            id: iconSpacingSlider
            text: i18n("Icon spacing")
            from: 0; to: 64; stepSize: 1
            value: DockSettings.iconSpacing
            valueText: i18nc("@label pixels", "%1 px", value)
            onMoved: (value) => DockSettings.iconSpacing = value
        }
    }

    // --- Zoom ---
    FormCard.FormHeader {
        title: i18n("Zoom")
    }

    FormCard.FormCard {
        SliderDelegate {
            id: zoomFactorSlider
            text: i18n("Zoom factor")
            from: 1.0; to: 2.0; stepSize: 0.1
            value: DockSettings.maxZoomFactor
            valueText: i18nc("@label zoom factor", "%1x", value.toFixed(1))
            minLabel: i18nc("@label zoom factor slider minimum", "No zoom")
            maxLabel: i18nc("@label zoom factor slider maximum", "Double size")
            onMoved: (value) => DockSettings.maxZoomFactor = value
        }

        FormCard.FormDelegateSeparator {}

        FormCard.AbstractFormDelegate {
            id: zoomStyleDelegate
            background: null
            enabled: page.zoomEnabled
            focusPolicy: Qt.NoFocus
            activeFocusOnTab: false
            Accessible.role: Accessible.Grouping
            Accessible.name: i18n("Zoom style")
            Accessible.description: i18n("How neighboring icons make room for the magnified icon")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Zoom style")
                    color: zoomStyleDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    Accessible.ignored: true
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("How neighboring icons make room for the magnified icon")
                    wrapMode: Text.Wrap
                    font: Kirigami.Theme.smallFont
                    color: page.secondaryTextColor
                    Accessible.ignored: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.largeSpacing
                    columns: Math.max(1, Math.min(2, Math.floor(width / (Kirigami.Units.gridUnit * 9))))
                    columnSpacing: Kirigami.Units.largeSpacing * 2
                    rowSpacing: Kirigami.Units.largeSpacing

                    ChoiceCard {
                        id: parabolicCard
                        text: i18n("Parabolic - neighbors move aside")
                        previewHeight: Kirigami.Units.gridUnit * 5
                        Binding { target: parabolicCard; property: "checked"; value: DockSettings.zoomStyle === 0 }
                        onChosen: DockSettings.zoomStyle = 0

                        DockTile {
                            dock.zoomStyle: 0
                            dock.maxZoomFactor: Math.max(DockSettings.maxZoomFactor, 1.5)
                            dock.hoveredIndex: dock.middleIndex
                        }
                    }

                    ChoiceCard {
                        id: inPlaceCard
                        text: i18n("In place - icons overlap")
                        previewHeight: Kirigami.Units.gridUnit * 5
                        Binding { target: inPlaceCard; property: "checked"; value: DockSettings.zoomStyle === 1 }
                        onChosen: DockSettings.zoomStyle = 1

                        DockTile {
                            dock.zoomStyle: 1
                            dock.maxZoomFactor: Math.max(DockSettings.maxZoomFactor, 1.5)
                            dock.hoveredIndex: dock.middleIndex
                        }
                    }
                }
            }
        }
    }

    // --- Zoom animation ---
    FormCard.FormHeader {
        title: i18n("Zoom animation")
    }

    FormCard.FormCard {
        enabled: page.zoomEnabled

        FormCard.AbstractFormDelegate {
            background: null
            contentItem: QQC2.TabBar {
                id: zoomAnimationTabs
                // Guards onCurrentIndexChanged until the Binding below has
                // applied: with a Custom preset the tab bar starts at index 0
                // and must not write the preset back before construction ends.
                property bool _zoomTabsReady: false
                Component.onCompleted: _zoomTabsReady = true

                // Plain item in place of the org.kde.desktop style's
                // background MouseArea, whose onWheel flipped tabs and
                // swallowed page scrolling under the pointer.
                background: Item {}

                // Binding element (not a property binding) so imperative
                // currentIndex writes — clicks, keys — re-sync to the setting.
                Binding {
                    target: zoomAnimationTabs
                    property: "currentIndex"
                    value: page.zoomCustom ? 1 : 0
                }

                // Covers every currentIndex change source (clicks reach us via
                // Container's internal checked->currentIndex write), so the
                // TabButtons need no onClicked handlers. Loop-safe: each branch
                // writes the preset only when it disagrees with the tab.
                onCurrentIndexChanged: {
                    if (!_zoomTabsReady) {
                        return
                    }
                    if (currentIndex === 1 && !page.zoomCustom) {
                        DockSettings.zoomAnimationPreset = page.zoomProfile.customPreset
                    } else if (currentIndex === 0 && page.zoomCustom) {
                        DockSettings.zoomAnimationPreset = page.lastZoomPreset
                    }
                }

                QQC2.TabButton {
                    text: i18n("Preset")
                    Accessible.name: text
                }

                QQC2.TabButton {
                    text: i18n("Custom")
                    Accessible.name: text
                }
            }
        }

        // Preset tab. Auto-exclusive cards keep a click on the checked card
        // from unchecking it; the per-card Binding re-applies the setting
        // after a click toggled `checked`, so programmatic preset changes
        // stay in sync.
        FormCard.FormDelegateSeparator { visible: !page.zoomCustom }

        FormCard.AbstractFormDelegate {
            visible: !page.zoomCustom
            background: null
            focusPolicy: Qt.NoFocus
            activeFocusOnTab: false
            Accessible.role: Accessible.Grouping
            Accessible.name: i18n("Zoom animation")

            contentItem: GridLayout {
                id: zoomPresetGrid
                columns: Math.max(1, Math.min(4, Math.floor(width / (Kirigami.Units.gridUnit * 8))))
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.largeSpacing

                Repeater {
                    model: page.zoomPresetChoices

                    ChoiceCard {
                        id: presetCard
                        required property var modelData
                        required property int index

                        readonly property var timing: page.zoomProfile.presets[index]

                        text: modelData.text
                        description: modelData.description
                        previewHeight: Kirigami.Units.gridUnit * 3.5

                        Binding { target: presetCard; property: "checked"; value: DockSettings.zoomAnimationPreset === presetCard.index }
                        onChosen: page.selectZoomPreset(presetCard.index)

                        // The middle icon zooms in and out in a loop with this
                        // preset's timing.
                        DockTile {
                            active: presetCard.visible && page.zoomEnabled && page.windowActive
                            dock.maxZoomFactor: Math.max(DockSettings.maxZoomFactor, 1.5)
                            dock.pulse: true
                            dock.zoomInDuration: page.effectiveDuration(presetCard.timing.inDuration)
                            dock.zoomOutDuration: page.effectiveDuration(presetCard.timing.outDuration)
                            dock.zoomInEasing: page.zoomProfile.easingType(presetCard.timing.inEasing)
                            dock.zoomOutEasing: page.zoomProfile.easingType(presetCard.timing.outEasing)
                        }
                    }
                }
            }
        }

        // Custom tab
        FormCard.FormDelegateSeparator { visible: page.zoomCustom }

        FormCard.AbstractFormDelegate {
            visible: page.zoomCustom
            background: null
            focusPolicy: Qt.NoFocus
            activeFocusOnTab: false
            Accessible.role: Accessible.Graphic
            Accessible.name: i18n("Custom zoom animation preview")
            Accessible.description: i18n("Zoom in %1 ms, zoom out %2 ms", DockSettings.zoomInDuration, DockSettings.zoomOutDuration)

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing * 2

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    ZoomCurve {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 3.5
                        inDuration: DockSettings.zoomInDuration
                        outDuration: DockSettings.zoomOutDuration
                        inEasing: DockSettings.zoomInEasing
                        outEasing: DockSettings.zoomOutEasing
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Zoom in %1 ms, zoom out %2 ms", DockSettings.zoomInDuration, DockSettings.zoomOutDuration)
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        font.features: ({ "tnum": 1 })
                        color: page.secondaryTextColor
                        wrapMode: Text.Wrap
                    }
                }

                // The middle icon zooms in and out with the custom timing.
                Item {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 4.5

                    DockTile {
                        radius: Kirigami.Units.cornerRadius * 2
                        active: visible && page.zoomEnabled && page.windowActive
                        dock.maxZoomFactor: Math.max(DockSettings.maxZoomFactor, 1.5)
                        dock.pulse: true
                        dock.zoomInDuration: page.effectiveDuration(DockSettings.zoomInDuration)
                        dock.zoomOutDuration: page.effectiveDuration(DockSettings.zoomOutDuration)
                        dock.zoomInEasing: page.zoomProfile.easingType(DockSettings.zoomInEasing)
                        dock.zoomOutEasing: page.zoomProfile.easingType(DockSettings.zoomOutEasing)
                    }
                }
            }
        }

        FormCard.FormDelegateSeparator { visible: page.zoomCustom }

        FormCard.FormSpinBoxDelegate {
            visible: page.zoomCustom
            label: i18n("Zoom-in duration (ms)")
            from: 0; to: 1000; stepSize: 10
            value: DockSettings.zoomInDuration
            onValueChanged: DockSettings.zoomInDuration = value
        }

        FormCard.FormDelegateSeparator { visible: page.zoomCustom }

        FormCard.FormComboBoxDelegate {
            visible: page.zoomCustom
            text: i18n("Zoom-in easing")
            Accessible.name: text
            model: page.zoomEasingNames
            currentIndex: DockSettings.zoomInEasing
            onActivated: function(index) { DockSettings.zoomInEasing = index }
        }

        FormCard.FormDelegateSeparator { visible: page.zoomCustom }

        FormCard.FormSpinBoxDelegate {
            visible: page.zoomCustom
            label: i18n("Zoom-out duration (ms)")
            from: 0; to: 1000; stepSize: 10
            value: DockSettings.zoomOutDuration
            onValueChanged: DockSettings.zoomOutDuration = value
        }

        FormCard.FormDelegateSeparator { visible: page.zoomCustom }

        FormCard.FormComboBoxDelegate {
            visible: page.zoomCustom
            text: i18n("Zoom-out easing")
            Accessible.name: text
            model: page.zoomEasingNames
            currentIndex: DockSettings.zoomOutEasing
            onActivated: function(index) { DockSettings.zoomOutEasing = index }
        }
    }

    // --- Icon appearance ---
    FormCard.FormHeader {
        title: i18n("Icon appearance")
    }

    FormCard.FormCard {
        RowLayout {
            Layout.fillWidth: true
            spacing: 0

            FormCard.FormSwitchDelegate {
                Layout.fillWidth: true
                text: i18n("Icon size normalization")
                description: i18n("Automatically adjust icons with excess padding to appear visually consistent")
                checked: DockSettings.iconNormalization
                onToggled: DockSettings.iconNormalization = checked
            }

            // Before/After: the same padded icon without and with padding removal.
            RowLayout {
                Layout.rightMargin: Kirigami.Units.largeSpacing * 2
                Layout.alignment: Qt.AlignVCenter
                spacing: Kirigami.Units.smallSpacing
                Accessible.role: Accessible.Graphic
                Accessible.name: i18n("Icon size normalization comparison")
                Accessible.description: i18n("The same icon before and after its internal padding is removed")

                NormalizationSample {
                    fill: 0.72
                    label: i18nc("@label icon normalization comparison", "Before")
                    current: !DockSettings.iconNormalization
                }

                Kirigami.Icon {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.bottomMargin: Kirigami.Units.gridUnit
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: implicitWidth
                    source: page.LayoutMirroring.enabled ? "go-previous-symbolic" : "go-next-symbolic"
                    color: page.secondaryTextColor
                }

                NormalizationSample {
                    fill: 1.0
                    label: i18nc("@label icon normalization comparison", "After")
                    current: DockSettings.iconNormalization
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        RowLayout {
            Layout.fillWidth: true
            spacing: 0

            SliderDelegate {
                Layout.fillWidth: true
                text: i18n("Icon scale")
                from: 0.5; to: 1.0; stepSize: 0.05
                value: DockSettings.iconScale
                valueText: i18nc("@label percent", "%1%", Math.round(value * 100))
                onMoved: (value) => DockSettings.iconScale = value
            }

            // Enlarged single icon: its cell vs the icon at the chosen scale.
            Rectangle {
                Layout.rightMargin: Kirigami.Units.largeSpacing * 2
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: Kirigami.Units.gridUnit * 3.5
                implicitHeight: implicitWidth
                radius: Kirigami.Units.cornerRadius * 2
                color: Kirigami.Theme.alternateBackgroundColor
                Accessible.role: Accessible.Graphic
                Accessible.name: i18n("Icon scale preview")
                Accessible.description: i18n("The icon fills %1% of its cell", Math.round(DockSettings.iconScale * 100))

                // Cell outline at full scale.
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    radius: Kirigami.Units.cornerRadius
                    color: Qt.alpha(Kirigami.Theme.highlightColor, 0)
                    border.width: 1
                    border.color: Qt.alpha(Kirigami.Theme.highlightColor, 0.5)
                }

                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: (parent.width - Kirigami.Units.smallSpacing * 2) * DockSettings.iconScale
                    height: width
                    source: "org.kde.konsole"
                    fallback: "utilities-terminal"
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            text: i18n("Active window icon opacity")
            from: 0.1; to: 1.0; stepSize: 0.05
            value: DockSettings.iconOpacityActive
            valueText: i18nc("@label percent", "%1%", Math.round(value * 100))
            onMoved: (value) => DockSettings.iconOpacityActive = value
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            text: i18n("Inactive window and launcher icon opacity")
            from: 0.1; to: 1.0; stepSize: 0.05
            value: DockSettings.iconOpacityInactive
            valueText: i18nc("@label percent", "%1%", Math.round(value * 100))
            onMoved: (value) => DockSettings.iconOpacityInactive = value
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            text: i18n("Minimized window icon opacity")
            from: 0.1; to: 1.0; stepSize: 0.05
            value: DockSettings.iconOpacityMinimized
            valueText: i18nc("@label percent", "%1%", Math.round(value * 100))
            onMoved: (value) => DockSettings.iconOpacityMinimized = value
        }
    }
}
