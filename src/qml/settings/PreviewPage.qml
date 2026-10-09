// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

SettingsPage {
    id: page

    title: i18n("Window Preview")
    subtitle: i18n("Thumbnails of an app's windows, shown above the dock on hover")

    // Clicking a grouped dock item can open the preview popup (GroupedWindowClickAction 1 =
    // "Show previews") even when hover previews are off, so the popup size and hide delay
    // stay meaningful in that case.
    readonly property bool groupedClickShowsPreviews: DockSettings.groupedWindowClickAction === 1
    readonly property bool popupInUse: DockSettings.previewEnabled || groupedClickShowsPreviews

    // The preview popup drawn on the stage: a miniature of PreviewPopup.qml /
    // PreviewThumbnail.qml — a rounded popup in theme colors with a header
    // (app icon, window title, close button) and a thumbnail showing a
    // miniature app window. Every length is `unit` preview pixels per real
    // pixel, so the thumbnail is as wide as the configured size at the stage's
    // scale.
    component MiniPreviewPopup: Item {
        id: popup

        /// Preview pixels per real pixel (DesktopStage.unit).
        property real unit: 0.1
        /// Looping transitions may run (pause while the window is hidden).
        property bool active: true
        /// 0 Top, 1 Bottom, 2 Left, 3 Right (DesktopStage.edge).
        property int edge: DockSettings.edge
        /// The dock item under the pointer, for anchoring on the icon center.
        property Item anchorIcon: null
        /// Shown while the popup can open; fades out otherwise.
        property bool shown: true
        property string appIcon: "org.kde.dolphin"
        property string appFallback: "system-file-manager"
        property string windowTitle: i18nc("@title sample window title in the preview mockup", "Home — Dolphin")

        /// The thumbnail bounds, for the stage's measurement overlay.
        readonly property alias thumbnailItem: thumbnail

        readonly property bool verticalEdge: edge === 2 || edge === 3
        // Popup size in real screen pixels (unit-independent), mirroring the
        // layout below: 2x largeSpacing padding, a small-icon header row, two
        // smallSpacing gaps, the separator and the 0.7-aspect thumbnail. The
        // stage uses it to pick a unit that keeps the popup inside the frame.
        readonly property real realHeight: 2 * Kirigami.Units.largeSpacing
            + Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing * 2 + 1
            + DockSettings.previewThumbnailSize * 0.7
        readonly property real realWidth: Math.max(DockSettings.previewThumbnailSize, 140)
            + 2 * Kirigami.Units.largeSpacing
        readonly property real pad: Kirigami.Units.largeSpacing * unit
        readonly property real spacing: Kirigami.Units.smallSpacing * unit
        // Same aspect as the real thumbnail (PreviewThumbnail.qml).
        readonly property real thumbWidth: DockSettings.previewThumbnailSize * unit
        readonly property real thumbHeight: thumbWidth * 0.7
        // Gap between the popup and the screen edge, like the dock's own margin.
        readonly property real edgeGap: Kirigami.Units.smallSpacing * unit
        // Miniature scale used for the drawing inside the thumbnail: 1/50 of
        // the real thumbnail width (so ~4 px at the default 200 px).
        readonly property real s: Math.max(0.05, thumbWidth / 50)

        readonly property bool _iconOk: anchorIcon !== null
        // `anchorIcon` center in this item's parent (the miniature screen).
        // Summing positions keeps the binding live while the icon zooms.
        readonly property real _iconCX: {
            let x = 0
            for (let it = popup._iconOk ? popup.anchorIcon : null; it && it !== popup.parent; it = it.parent) {
                x += it.x
            }
            return popup._iconOk ? x + popup.anchorIcon.width / 2 : (popup.parent ? popup.parent.width / 2 : 0)
        }
        readonly property real _iconCY: {
            let y = 0
            for (let it = popup._iconOk ? popup.anchorIcon : null; it && it !== popup.parent; it = it.parent) {
                y += it.y
            }
            return popup._iconOk ? y + popup.anchorIcon.height / 2 : (popup.parent ? popup.parent.height / 2 : 0)
        }

        opacity: shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity {
            enabled: popup.active
            NumberAnimation { duration: Kirigami.Units.longDuration; easing.type: Easing.InOutQuad }
        }

        implicitWidth: box.width
        implicitHeight: box.height

        // The popup hovers over the hovered icon, clear of its zoomed bounds
        // with a small gap — like PreviewPopup.qml. Horizontal edges center it
        // on the icon; vertical edges hug the edge and center on the icon
        // vertically. Clamped to the part of the miniature screen the stage
        // frame shows (the stage crops the screen when zoomed in).
        readonly property real _iconHalf: popup._iconOk
            ? (verticalEdge ? popup.anchorIcon.height : popup.anchorIcon.width) / 2 : 0
        readonly property real _minX: parent ? Math.max(0, -parent.x) : 0
        readonly property real _maxX: parent ? Math.min(parent.width, (parent.parent ? parent.parent.width : parent.width) - parent.x) - width : 0
        readonly property real _minY: parent ? Math.max(0, -parent.y) : 0
        readonly property real _maxY: parent ? Math.min(parent.height, (parent.parent ? parent.parent.height : parent.height) - parent.y) - height : 0
        x: parent ? (verticalEdge
            ? (edge === 2 ? edgeGap : parent.width - width - edgeGap)
            : Math.max(_minX, Math.min(_maxX, _iconCX - width / 2))) : 0
        y: parent ? (verticalEdge
            ? Math.max(_minY, Math.min(_maxY, _iconCY - height / 2))
            : Math.max(_minY, edge === 0
                ? _iconCY + _iconHalf + edgeGap
                : _iconCY - _iconHalf - edgeGap - height)) : 0

        Kirigami.ShadowedRectangle {
            id: box
            width: popupLayout.implicitWidth + popup.pad * 2
            height: popupLayout.implicitHeight + popup.pad * 2
            radius: Kirigami.Units.cornerRadius
            color: Kirigami.Theme.backgroundColor
            border.width: 1
            border.color: Qt.alpha(Kirigami.Theme.textColor, 0.25)
            shadow.size: Kirigami.Units.gridUnit * 0.4
            shadow.yOffset: 1
            shadow.color: Qt.alpha(Qt.darker(Kirigami.Theme.backgroundColor, 4.0), 0.35)

            Kirigami.Theme.colorSet: Kirigami.Theme.Window
            Kirigami.Theme.inherit: false

            ColumnLayout {
                id: popupLayout
                anchors.fill: parent
                anchors.margins: popup.pad
                spacing: popup.spacing

                // Header: app icon + window title + close button.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: popup.spacing

                    Kirigami.Icon {
                        source: popup.appIcon
                        fallback: popup.appFallback
                        implicitWidth: Math.max(4, Kirigami.Units.iconSizes.small * popup.unit)
                        implicitHeight: implicitWidth
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: popup.windowTitle
                        font.pixelSize: Math.max(4, Kirigami.Theme.defaultFont.pixelSize * popup.unit)
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }
                    // Close button: circle with the real window-close icon.
                    Rectangle {
                        readonly property real d: Math.max(4, Kirigami.Units.iconSizes.small * popup.unit)
                        implicitWidth: d
                        implicitHeight: d
                        radius: d / 2
                        color: Qt.alpha(Kirigami.Theme.textColor, 0.12)

                        Kirigami.Icon {
                            anchors.centerIn: parent
                            width: parent.width * 0.7
                            height: width
                            source: "window-close"
                        }
                    }
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                // Thumbnail: a miniature app window (title bar with window
                // controls and title, toolbar, sidebar and file list).
                Rectangle {
                    id: thumbnail
                    Layout.preferredWidth: popup.thumbWidth
                    Layout.preferredHeight: popup.thumbHeight
                    Layout.alignment: Qt.AlignHCenter
                    radius: Kirigami.Units.smallSpacing * popup.unit
                    color: Kirigami.Theme.alternateBackgroundColor
                    clip: true

                    Item {
                        anchors.fill: parent

                        Rectangle {
                            id: winTitleBar
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: Math.max(2, thumbnail.height * 0.16)
                            color: Qt.alpha(Kirigami.Theme.textColor, 0.08)

                            QQC2.Label {
                                anchors.centerIn: parent
                                width: parent.width * 0.7
                                text: i18nc("@title folder name in the sample window title bar", "Home")
                                font.pixelSize: Math.max(1, parent.height * 0.42)
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                color: Kirigami.Theme.textColor
                                opacity: 0.8
                                Accessible.ignored: true
                            }

                            // Window controls on the right (Breeze order:
                            // minimize, maximize, close).
                            Row {
                                anchors.right: parent.right
                                anchors.rightMargin: popup.s * 0.8
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: popup.s * 0.8

                                Repeater {
                                    model: [0.35, 0.35, 0.9]
                                    delegate: Rectangle {
                                        required property real modelData
                                        required property int index
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Math.max(1, popup.s * 0.9)
                                        height: width
                                        radius: width / 2
                                        color: index === 2
                                            ? Kirigami.Theme.negativeTextColor
                                            : Qt.alpha(Kirigami.Theme.textColor, modelData)
                                    }
                                }
                            }
                        }

                        // Toolbar: navigation buttons and the address field.
                        Row {
                            id: winToolbar
                            anchors.top: winTitleBar.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: popup.s
                            anchors.topMargin: popup.s * 0.8
                            spacing: popup.s * 0.8

                            Repeater {
                                model: 2
                                Rectangle {
                                    width: popup.s * 2
                                    height: Math.max(1, popup.s * 0.9)
                                    radius: height / 2
                                    color: Qt.alpha(Kirigami.Theme.textColor, 0.25)
                                }
                            }
                            Rectangle {
                                width: Math.max(popup.s * 4, parent.width - popup.s * 5.6)
                                height: Math.max(1, popup.s * 1.1)
                                radius: height / 2
                                color: Qt.alpha(Kirigami.Theme.textColor, 0.12)
                            }
                        }

                        // Body: the Places sidebar next to the file list.
                        Item {
                            anchors.top: winToolbar.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: popup.s
                            anchors.topMargin: popup.s * 0.8

                            Column {
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                width: parent.width * 0.3
                                spacing: popup.s * 0.7

                                Repeater {
                                    model: [0.9, 0.7, 0.8, 0.6]
                                    delegate: Rectangle {
                                        required property int index
                                        required property real modelData
                                        width: parent.width * modelData
                                        height: Math.max(1, popup.s * 0.9)
                                        radius: height / 2
                                        color: index === 0
                                            ? Kirigami.Theme.highlightColor
                                            : Qt.alpha(Kirigami.Theme.textColor, 0.18)
                                        opacity: index === 0 ? 0.8 : 1.0
                                    }
                                }
                            }

                            Column {
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.right: parent.right
                                width: parent.width * 0.62
                                spacing: popup.s * 0.7

                                Repeater {
                                    model: [0.95, 0.7, 0.85, 0.5, 0.75, 0.6]
                                    delegate: Row {
                                        id: fileRow
                                        required property real modelData
                                        width: parent.width
                                        spacing: popup.s * 0.5

                                        Rectangle {
                                            width: Math.max(1, popup.s * 0.8)
                                            height: Math.max(1, popup.s * 0.8)
                                            radius: width * 0.25
                                            color: Qt.alpha(Kirigami.Theme.highlightColor, 0.45)
                                        }
                                        Rectangle {
                                            width: fileRow.width * fileRow.modelData * 0.85
                                            height: Math.max(1, popup.s * 0.7)
                                            radius: height / 2
                                            color: Qt.alpha(Kirigami.Theme.textColor, 0.3)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Stage: the desktop with the dock and a live preview popup ---

    stage: Component {
        DesktopStage {
            id: stage

            readonly property real coverUnit: Math.max(width / screenSize.width, height / screenSize.height)
            // Real pixels of screen edge the hovered icon reaches (floating
            // margin + padding + zoomed icon) plus the popup gap.
            readonly property real _iconSpanReal: 8 + Kirigami.Units.largeSpacing
                + dock.iconSize * Math.max(1, dock.maxZoomFactor) + Kirigami.Units.smallSpacing
            // Scale that keeps the hovered icon, the gap and the whole popup
            // inside the frame with a margin. Real-pixel sizes only, so it
            // does not depend on `unit` (no binding loop). Horizontal dock:
            // icon + popup stacked across the edge; vertical dock: the popup
            // hugs the edge and is centered along it.
            readonly property real _fitUnit: {
                const m = Kirigami.Units.largeSpacing * 2
                const across = dock.vertical ? width : height
                const along = dock.vertical ? height : width
                const acrossReal = dock.vertical
                    ? Kirigami.Units.smallSpacing + popup.realWidth
                    : _iconSpanReal + popup.realHeight
                const alongReal = dock.vertical ? popup.realHeight : popup.realWidth
                return Math.min((across - m) / Math.max(1, acrossReal),
                                (along - m) / Math.max(1, alongReal))
            }

            active: page.windowActive
            // A taller stage leaves room for the popup over the miniature.
            maximumHeight: Kirigami.Units.gridUnit * 15
            // Zoomed in around the dock like the other stages, but never past
            // the scale where the popup still fits.
            unit: Math.max(coverUnit, Math.min(framingUnit, _fitUnit,
                0.85 * (dock.vertical ? height : width) / dock.restLengthReal))

            Accessible.role: Accessible.Graphic
            Accessible.name: i18n("Dock with a window preview")
            Accessible.description: i18n("Sample desktop showing the preview popup above a hovered dock icon")

            measureOrientation: Qt.Horizontal
            measureTarget: thumbnailWidthSlider.active ? popup.thumbnailItem : null
            measureText: i18nc("@label pixels", "%1 px", DockSettings.previewThumbnailSize)

            MiniDock {
                id: dock
                unit: stage.unit
                backdrop: stage.backdrop
                active: stage.active
                // The Dolphin icon is hovered while previews can open, so the
                // popup (a Dolphin window) sits over that zoomed icon like the
                // real dock.
                readonly property int previewAppIndex: Math.max(0, apps.findIndex(app => app.icon === "org.kde.dolphin"))
                hoveredIndex: page.popupInUse ? previewAppIndex : -1
            }

            MiniPreviewPopup {
                id: popup
                unit: stage.unit
                active: stage.active
                edge: stage.edge
                // Stays on the Dolphin icon while fading out (focusIcon falls
                // back to the middle icon once nothing is hovered).
                Binding on anchorIcon {
                    when: page.popupInUse
                    value: dock.focusIcon
                    restoreMode: Binding.RestoreNone
                }
                shown: page.popupInUse
            }
        }
    }

    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18n("Show window previews on hover")
            description: i18n("Show window thumbnails when hovering dock items")
            checked: DockSettings.previewEnabled
            onToggled: DockSettings.previewEnabled = checked
        }

        FormCard.FormDelegateSeparator {
            visible: groupedClickNote.visible
        }

        FormCard.FormTextDelegate {
            id: groupedClickNote
            visible: !DockSettings.previewEnabled && page.groupedClickShowsPreviews
            text: i18n("Previews still open on click")
            description: i18n("Clicking an app with several windows shows previews, so the thumbnail width and hide delay below still apply.")
        }
    }

    FormCard.FormHeader {
        title: i18n("Settings")
    }

    FormCard.FormCard {
        SliderDelegate {
            id: thumbnailWidthSlider
            text: i18n("Thumbnail width (px)")
            from: 120; to: 320; stepSize: 20
            value: DockSettings.previewThumbnailSize
            valueText: i18nc("@label pixels", "%1 px", value)
            enabled: page.popupInUse
            onMoved: (value) => DockSettings.previewThumbnailSize = value
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            text: i18n("Hover delay (ms)")
            from: 0; to: 2000; stepSize: 50
            value: DockSettings.previewHoverDelay
            valueText: i18nc("@label milliseconds", "%1 ms", value)
            enabled: DockSettings.previewEnabled
            onMoved: (value) => DockSettings.previewHoverDelay = value
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            text: i18n("Hide delay (ms)")
            from: 0; to: 1000; stepSize: 50
            value: DockSettings.previewHideDelay
            valueText: i18nc("@label milliseconds", "%1 ms", value)
            enabled: page.popupInUse
            onMoved: (value) => DockSettings.previewHideDelay = value
        }
    }
}
