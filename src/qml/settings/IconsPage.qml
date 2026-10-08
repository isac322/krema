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

FormCard.FormCardPage {
    id: page
    title: i18n("Icons")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

    readonly property ZoomAnimationProfile zoomProfile: ZoomAnimationProfile {}
    readonly property bool zoomCustom: DockSettings.zoomAnimationPreset === zoomProfile.customPreset
    // Last non-custom preset chosen in this page session; the Preset tab restores it.
    // Seeded once (not bound) so switching to Custom does not reset it.
    property int lastZoomPreset: 0

    readonly property bool zoomEnabled: DockSettings.maxZoomFactor > 1.0
    // Looping preview animations only run while the settings window is shown.
    readonly property bool windowVisible: Window.window ? Window.window.visible : false

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

    // Fill colour of the schematic icon tiles in the small card previews.
    readonly property color sketchTileColor: Kirigami.ColorUtils.linearInterpolation(
        Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.35)

    // --- Inline preview components ---

    // Five schematic icons with the middle one magnified, laid out the way a
    // zoom style makes room for it.
    component ZoomStyleSketch: Item {
        id: sketch

        property bool parabolic
        readonly property var scales: [1.0, 1.35, 1.7, 1.35, 1.0]
        readonly property real unit: Math.min(height / 2.4, width / 8.4)

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            width: sketchRow.width + sketch.unit * 0.6
            height: sketch.unit * 1.5
            radius: sketch.unit * 0.35
            color: Kirigami.Theme.alternateBackgroundColor
        }

        Row {
            id: sketchRow
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: sketch.unit * 0.25
            spacing: sketch.unit * 0.2

            Repeater {
                model: sketch.scales

                Item {
                    required property real modelData
                    required property int index

                    width: sketch.parabolic ? sketch.unit * modelData : sketch.unit
                    height: sketch.unit
                    z: modelData

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        width: sketch.unit
                        height: sketch.unit
                        radius: sketch.unit * 0.25
                        scale: parent.modelData
                        transformOrigin: Item.Bottom
                        color: parent.index === 2 ? Kirigami.Theme.highlightColor : page.sketchTileColor
                        // Outline in the card colour makes overlapping tiles readable.
                        border.width: 1
                        border.color: Kirigami.Theme.backgroundColor
                    }
                }
            }
        }
    }

    // One icon on a dock strip that zooms in and out in a loop with the given
    // timing. Durations are effective (already scaled) milliseconds; easings
    // are Easing.Type values.
    component ZoomMotion: Item {
        id: motion

        property int inDuration
        property int outDuration
        property int inEasing: Easing.InOutCubic
        property int outEasing: Easing.InOutCubic
        property bool active
        property real amount: 0

        readonly property real unit: Math.min(height / 2.2, width / 3)

        function restartIfRunning() {
            if (motionAnimation.running) {
                motionAnimation.restart()
            }
        }
        onInDurationChanged: restartIfRunning()
        onOutDurationChanged: restartIfRunning()
        onInEasingChanged: restartIfRunning()
        onOutEasingChanged: restartIfRunning()

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            width: motion.unit * 2.4
            height: motion.unit * 1.3
            radius: motion.unit * 0.3
            color: Kirigami.Theme.alternateBackgroundColor
        }

        Kirigami.Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: motion.unit * 0.15
            // Rendered at the full zoom size and scaled down, so the zoomed
            // icon stays crisp (never magnified past its rendered size).
            readonly property real maxZoom: 1.7
            width: motion.unit * maxZoom
            height: width
            source: "preferences-system"
            scale: (1.0 + (maxZoom - 1.0) * motion.amount) / maxZoom
            transformOrigin: Item.Bottom
        }

        SequentialAnimation {
            id: motionAnimation
            loops: Animation.Infinite
            running: motion.active
            onRunningChanged: if (!running) motion.amount = 0

            PauseAnimation { duration: 500 }
            NumberAnimation {
                target: motion
                property: "amount"
                to: 1
                duration: motion.inDuration
                easing.type: motion.inEasing
            }
            PauseAnimation { duration: 700 }
            NumberAnimation {
                target: motion
                property: "amount"
                to: 0
                duration: motion.outDuration
                easing.type: motion.outEasing
            }
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
            color: Kirigami.Theme.disabledTextColor
        }
        Rectangle {
            x: curve.margin
            y: curve.margin
            width: curve.width - curve.margin * 2
            height: 1
            color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.disabledTextColor, 0.5)
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: Kirigami.Theme.highlightColor
                strokeWidth: 2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin

                PathPolyline {
                    path: curve.points
                }
            }
        }
    }

    // A padded icon inside its cell outline, for the normalization comparison.
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
            radius: Kirigami.Units.smallSpacing
            color: Qt.alpha(Kirigami.Theme.textColor, 0.04)
            border.width: sample.current ? 2 : 1
            border.color: sample.current
                ? Kirigami.Theme.highlightColor
                : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.3)

            Kirigami.Icon {
                anchors.centerIn: parent
                width: (parent.width - 4) * sample.fill
                height: width
                source: "application-x-executable"
            }
        }

        QQC2.Label {
            Layout.alignment: Qt.AlignHCenter
            text: sample.label
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            font.bold: sample.current
            color: sample.current ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
        }
    }

    // --- Mini Dock Preview ---
    FormCard.FormCard {
        id: previewCard

        FormCard.AbstractFormDelegate {
            id: dockPreviewDelegate
            background: null
            focusPolicy: Qt.NoFocus
            activeFocusOnTab: false
            Accessible.role: Accessible.Graphic
            Accessible.name: i18n("Dock preview")
            Accessible.description: i18n("Sample dock showing the icon size, spacing, zoom, zoom animation, normalization, scale and icon opacities")

            contentItem: Item {
                id: previewArea
                implicitHeight: previewStage.height * previewStage.scale + Kirigami.Units.smallSpacing + hoverToggleRow.implicitHeight

                Item {
                    id: previewStage

                    // Icons chosen for: (1) guaranteed on KDE (Breeze), (2) mix of
                    // padding-heavy vs padding-free to demonstrate normalization.
                    // "application-x-executable" and "preferences-desktop-theme" have
                    // visible internal padding; "folder" and "utilities-terminal" fill
                    // the canvas, making normalization differences obvious.
                    readonly property var iconNames: [
                        "system-file-manager",
                        "utilities-terminal",
                        "application-x-executable",
                        "preferences-system",
                        "internet-web-browser",
                        "accessories-text-editor",
                        "preferences-desktop-theme"
                    ]
                    // Window state per icon, so every opacity setting is visible.
                    readonly property var iconStates: [
                        "inactive", "active", "launcher", "inactive", "minimized", "inactive", "launcher"
                    ]

                    readonly property real size: DockSettings.iconSize
                    readonly property real gap: DockSettings.iconSpacing
                    readonly property int count: iconNames.length
                    readonly property real pitch: size + gap
                    readonly property real restWidth: count * size + (count - 1) * gap
                    readonly property real pad: Kirigami.Units.largeSpacing
                    readonly property real sigma: size * 1.2
                    readonly property bool parabolic: DockSettings.zoomStyle !== 1
                    // Upper bound of the total width Parabolic zoom adds.
                    readonly property real maxGrowth: parabolic
                        ? (DockSettings.maxZoomFactor - 1.0) * size * sigma * Math.sqrt(Math.PI) / pitch
                        : 0
                    readonly property real restLeft: (width - restWidth) / 2

                    width: restWidth + pad * 2 + maxGrowth
                    height: size * DockSettings.maxZoomFactor + pad * 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    transformOrigin: Item.Top
                    scale: Math.min(1.0, previewArea.width / width)

                    // Static hover: the middle icon is magnified by default.
                    property bool showStaticHover: true
                    readonly property real staticCursor: Math.floor(count / 2) * pitch + size / 2
                    // Cursor in rest-row coordinates, kept when the pointer leaves
                    // so zoom-out collapses around where it left (as in the dock).
                    property real mouseCursor: staticCursor
                    // Not readonly: a Behavior intercepts its writes.
                    property real cursor: previewHoverArea.containsMouse || !showStaticHover
                        ? mouseCursor
                        : staticCursor
                    Behavior on cursor {
                        enabled: !previewHoverArea.containsMouse
                        NumberAnimation {
                            duration: Kirigami.Units.longDuration
                            easing.type: Easing.OutCubic
                        }
                    }

                    // 0 = rest, 1 = full zoom; animated with the current zoom
                    // animation preset or custom timing, like the dock.
                    property real zoomAmount: previewHoverArea.containsMouse || showStaticHover ? 1.0 : 0.0
                    Behavior on zoomAmount {
                        id: previewZoomBehavior
                        enabled: page.zoomProfile.animated
                        NumberAnimation {
                            duration: previewZoomBehavior.targetValue > 0.5
                                ? page.zoomProfile.effectiveZoomInDuration
                                : page.zoomProfile.effectiveZoomOutDuration
                            easing.type: previewZoomBehavior.targetValue > 0.5
                                ? page.zoomProfile.zoomInEasingType
                                : page.zoomProfile.zoomOutEasingType
                        }
                    }

                    // Gaussian magnification, same curve as the dock.
                    readonly property var zoomScales: {
                        const extra = (DockSettings.maxZoomFactor - 1.0) * zoomAmount
                        const result = []
                        for (let i = 0; i < count; ++i) {
                            const d = cursor - (i * pitch + size / 2)
                            result.push(1.0 + extra * Math.exp(-(d * d) / (sigma * sigma)))
                        }
                        return result
                    }
                    // Parabolic: growth left of the cursor shifts the row left so
                    // the icon under the cursor stays put.
                    readonly property real leftGrowth: {
                        if (!parabolic) {
                            return 0
                        }
                        let growth = 0
                        for (let i = 0; i < count; ++i) {
                            const before = Math.max(0, Math.min(1, (cursor - (i * pitch + size / 2)) / pitch + 0.5))
                            growth += (zoomScales[i] - 1.0) * size * before
                        }
                        return growth
                    }

                    function paddingRatio(index) {
                        if (DockSettings.iconNormalization) {
                            return 1.0
                        }
                        // Simulated internal padding of padding-heavy icons,
                        // which normalization removes in the real dock.
                        switch (index) {
                        case 2: return 0.78  // application-x-executable
                        case 6: return 0.82  // preferences-desktop-theme
                        case 3: return 0.90  // preferences-system
                        default: return 1.0
                        }
                    }

                    function stateOpacity(state) {
                        switch (state) {
                        case "active": return DockSettings.iconOpacityActive
                        case "minimized": return DockSettings.iconOpacityMinimized
                        default: return DockSettings.iconOpacityInactive
                        }
                    }

                    // Dock background
                    Rectangle {
                        x: previewRow.x - previewStage.pad
                        width: previewRow.width + previewStage.pad * 2
                        height: previewStage.size + previewStage.pad * 2
                        anchors.bottom: parent.bottom
                        radius: Kirigami.Units.largeSpacing
                        color: Kirigami.Theme.alternateBackgroundColor
                    }

                    Row {
                        id: previewRow
                        x: previewStage.restLeft - previewStage.leftGrowth
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: previewStage.pad
                        height: previewStage.size
                        spacing: previewStage.gap

                        Repeater {
                            model: previewStage.iconNames

                            Item {
                                id: previewItem
                                required property string modelData
                                required property int index

                                readonly property real zoomScale: previewStage.zoomScales[index] ?? 1.0
                                readonly property string windowState: previewStage.iconStates[index]

                                // Parabolic: the slot widens and pushes neighbours
                                // aside. In place: the slot keeps its width and
                                // the icon magnifies over its neighbours.
                                width: previewStage.parabolic ? previewStage.size * zoomScale : previewStage.size
                                height: previewStage.size
                                z: zoomScale

                                // Cell rendered at the maximum zoom size and
                                // scaled DOWN to the current zoom from the
                                // bottom edge, so magnified icons stay crisp.
                                Item {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.bottom: parent.bottom
                                    width: previewStage.size * DockSettings.maxZoomFactor
                                    height: width
                                    scale: previewItem.zoomScale / DockSettings.maxZoomFactor
                                    transformOrigin: Item.Bottom

                                    Kirigami.Icon {
                                        anchors.centerIn: parent
                                        width: parent.width * DockSettings.iconScale * previewStage.paddingRatio(previewItem.index)
                                        height: width
                                        source: previewItem.modelData
                                        opacity: previewStage.stateOpacity(previewItem.windowState)
                                    }
                                }

                                // Running indicator (launchers have none)
                                Rectangle {
                                    visible: previewItem.windowState !== "launcher"
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.top: parent.bottom
                                    anchors.topMargin: (previewStage.pad - height) / 2
                                    width: Math.max(4, Math.round(previewStage.size / 10))
                                    height: width
                                    radius: width / 2
                                    color: previewItem.windowState === "active"
                                        ? Kirigami.Theme.highlightColor
                                        : Kirigami.Theme.textColor
                                    opacity: previewItem.windowState === "active" ? 1.0
                                        : previewItem.windowState === "minimized" ? 0.35 : 0.6
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: previewHoverArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                        onPositionChanged: mouse => previewStage.mouseCursor = mouse.x - previewStage.restLeft
                    }
                }

                // Toggle: show/hide static hover zoom in preview
                RowLayout {
                    id: hoverToggleRow
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        text: i18n("Hover")
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        color: Kirigami.Theme.disabledTextColor
                        Accessible.ignored: true
                    }
                    QQC2.Switch {
                        checked: previewStage.showStaticHover
                        onToggled: previewStage.showStaticHover = checked
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

    FormSection {
        SliderDelegate {
            Layout.fillWidth: true
            text: i18n("Icon size")
            from: 24; to: 96; stepSize: 4
            value: DockSettings.iconSize
            valueText: i18nc("@label pixels", "%1 px", value)
            onMoved: (value) => DockSettings.iconSize = value
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            Layout.fillWidth: true
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

    FormSection {
        SliderDelegate {
            Layout.fillWidth: true
            text: i18n("Zoom factor")
            from: 1.0; to: 2.0; stepSize: 0.1
            value: DockSettings.maxZoomFactor
            valueText: i18nc("@label zoom factor", "%1x", value.toFixed(1))
            onMoved: (value) => DockSettings.maxZoomFactor = value
        }

        FormCard.FormDelegateSeparator {}

        FormCard.AbstractFormDelegate {
            id: zoomStyleDelegate
            Layout.fillWidth: true
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
                    color: Kirigami.Theme.disabledTextColor
                    Accessible.ignored: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    columns: Math.max(1, Math.min(2, Math.floor(width / (Kirigami.Units.gridUnit * 9))))
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.largeSpacing

                    ChoiceCard {
                        id: parabolicCard
                        Layout.fillWidth: true
                        text: i18n("Parabolic - neighbors move aside")
                        previewHeight: Kirigami.Units.gridUnit * 3
                        Binding { target: parabolicCard; property: "checked"; value: DockSettings.zoomStyle === 0 }
                        onChosen: DockSettings.zoomStyle = 0

                        ZoomStyleSketch {
                            anchors.fill: parent
                            parabolic: true
                        }
                    }

                    ChoiceCard {
                        id: inPlaceCard
                        Layout.fillWidth: true
                        text: i18n("In place - icons overlap")
                        previewHeight: Kirigami.Units.gridUnit * 3
                        Binding { target: inPlaceCard; property: "checked"; value: DockSettings.zoomStyle === 1 }
                        onChosen: DockSettings.zoomStyle = 1

                        ZoomStyleSketch {
                            anchors.fill: parent
                            parabolic: false
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

    FormSection {
        enabled: page.zoomEnabled

        FormCard.AbstractFormDelegate {
            Layout.fillWidth: true
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
            Layout.fillWidth: true
            visible: !page.zoomCustom
            background: null
            focusPolicy: Qt.NoFocus
            activeFocusOnTab: false
            Accessible.role: Accessible.Grouping
            Accessible.name: i18n("Zoom animation")

            contentItem: GridLayout {
                id: zoomPresetGrid
                columns: Math.max(1, Math.min(4, Math.floor(width / (Kirigami.Units.gridUnit * 9))))
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.largeSpacing

                Repeater {
                    model: page.zoomPresetChoices

                    ChoiceCard {
                        id: presetCard
                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: modelData.text
                        description: modelData.description
                        previewHeight: Kirigami.Units.gridUnit * 3

                        Binding { target: presetCard; property: "checked"; value: DockSettings.zoomAnimationPreset === presetCard.index }
                        onChosen: page.selectZoomPreset(presetCard.index)

                        ZoomMotion {
                            readonly property var timing: page.zoomProfile.presets[presetCard.index]
                            anchors.fill: parent
                            inDuration: page.effectiveDuration(timing.inDuration)
                            outDuration: page.effectiveDuration(timing.outDuration)
                            inEasing: page.zoomProfile.easingType(timing.inEasing)
                            outEasing: page.zoomProfile.easingType(timing.outEasing)
                            active: visible && page.zoomEnabled && page.windowVisible
                        }
                    }
                }
            }
        }

        // Custom tab
        FormCard.FormDelegateSeparator { visible: page.zoomCustom }

        FormCard.AbstractFormDelegate {
            Layout.fillWidth: true
            visible: page.zoomCustom
            background: null
            focusPolicy: Qt.NoFocus
            activeFocusOnTab: false
            Accessible.role: Accessible.Graphic
            Accessible.name: i18n("Custom zoom animation preview")
            Accessible.description: i18n("Zoom in %1 ms, zoom out %2 ms", DockSettings.zoomInDuration, DockSettings.zoomOutDuration)

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    ZoomCurve {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                        inDuration: DockSettings.zoomInDuration
                        outDuration: DockSettings.zoomOutDuration
                        inEasing: DockSettings.zoomInEasing
                        outEasing: DockSettings.zoomOutEasing
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Zoom in %1 ms, zoom out %2 ms", DockSettings.zoomInDuration, DockSettings.zoomOutDuration)
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.disabledTextColor
                        wrapMode: Text.Wrap
                    }
                }

                ZoomMotion {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                    inDuration: page.effectiveDuration(DockSettings.zoomInDuration)
                    outDuration: page.effectiveDuration(DockSettings.zoomOutDuration)
                    inEasing: page.zoomProfile.easingType(DockSettings.zoomInEasing)
                    outEasing: page.zoomProfile.easingType(DockSettings.zoomOutEasing)
                    active: visible && page.zoomEnabled && page.windowVisible
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

    FormSection {
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
                Layout.rightMargin: Kirigami.Units.largeSpacing
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
                    color: Kirigami.Theme.disabledTextColor
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

            // Enlarged single icon: cell outline vs icon at the chosen scale.
            Rectangle {
                Layout.rightMargin: Kirigami.Units.largeSpacing
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: Kirigami.Units.gridUnit * 3.5
                implicitHeight: implicitWidth
                radius: Kirigami.Units.smallSpacing
                color: Qt.alpha(Kirigami.Theme.textColor, 0.04)
                border.width: 1
                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.3)
                Accessible.role: Accessible.Graphic
                Accessible.name: i18n("Icon scale preview")
                Accessible.description: i18n("The icon fills %1% of its cell", Math.round(DockSettings.iconScale * 100))

                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: (parent.width - 2) * DockSettings.iconScale
                    height: width
                    source: "utilities-terminal"
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            Layout.fillWidth: true
            text: i18n("Active window icon opacity")
            from: 0.1; to: 1.0; stepSize: 0.05
            value: DockSettings.iconOpacityActive
            valueText: i18nc("@label percent", "%1%", Math.round(value * 100))
            onMoved: (value) => DockSettings.iconOpacityActive = value
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            Layout.fillWidth: true
            text: i18n("Inactive window and launcher icon opacity")
            from: 0.1; to: 1.0; stepSize: 0.05
            value: DockSettings.iconOpacityInactive
            valueText: i18nc("@label percent", "%1%", Math.round(value * 100))
            onMoved: (value) => DockSettings.iconOpacityInactive = value
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            Layout.fillWidth: true
            text: i18n("Minimized window icon opacity")
            from: 0.1; to: 1.0; stepSize: 0.05
            value: DockSettings.iconOpacityMinimized
            valueText: i18nc("@label percent", "%1%", Math.round(value * 100))
            onMoved: (value) => DockSettings.iconOpacityMinimized = value
        }
    }
}
