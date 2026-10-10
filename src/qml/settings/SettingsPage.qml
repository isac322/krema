// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard

// Root of every settings page: a large page title with a one-line subtitle,
// an optional `stage` (usually a DesktopStage) and then the page's groups as
// FormCard.FormHeader + FormCard.FormCard children, all in one centered
// column of at most `maximumContentWidth`.
//
//     SettingsPage {
//         id: page
//         title: i18n("Icons")
//         subtitle: i18n("Size, spacing and hover zoom of the dock icons")
//         stage: Component {
//             DesktopStage {
//                 id: stage
//                 active: page.windowActive
//                 MiniDock { unit: stage.unit; backdrop: stage.backdrop; active: stage.active }
//             }
//         }
//
//         FormCard.FormHeader { title: i18n("Size") }
//         FormCard.FormCard {
//             SliderDelegate { ... }
//             FormCard.FormDelegateSeparator {}
//             FormCard.FormSwitchDelegate { ... }
//         }
//     }
//
// FormCard/FormHeader children that keep their default `maximumWidth` get
// `maximumContentWidth`, so groups line up with the title and the stage;
// headers also get more space above them than below.
FormCard.FormCardPage {
    id: page

    /// One line under the title saying what the page changes.
    property string subtitle
    /// Hero shown under the header (e.g. a DesktopStage); optional.
    property Component stage
    /// The item created from `stage`, or null.
    readonly property alias stageItem: stageLoader.item

    /// Width of the centered content column: at most 38 grid units, keeping
    /// empty margins at the sides (wheel scrolling there never hits a
    /// control) unless the page is narrow.
    readonly property real maximumContentWidth: Math.min(Kirigami.Units.gridUnit * 38,
        Math.max(Kirigami.Units.gridUnit * 20, width - Kirigami.Units.gridUnit * 5))
    /// The window is shown and not minimized: stage animations may run
    /// (a minimized window keeps animations ticking).
    readonly property bool windowActive: Window.window ? Window.window.visible && Window.window.visibility !== Window.Minimized : false

    readonly property color secondaryTextColor: Kirigami.ColorUtils.linearInterpolation(
        Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.7)

    bottomPadding: Kirigami.Units.gridUnit * 2

    // FormCard's and FormHeader's default maximumWidth.
    readonly property real _formDefaultWidth: Kirigami.Units.gridUnit * 30
    readonly property real _headerDefaultTopPadding: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing

    Component.onCompleted: {
        const items = pageHeader.parent.children
        for (let i = 0; i < items.length; ++i) {
            const item = items[i]
            if (item === pageHeader || item.maximumWidth === undefined || item.cardWidthRestricted === undefined) {
                continue
            }
            if (item.maximumWidth === _formDefaultWidth) {
                item.maximumWidth = Qt.binding(() => page.maximumContentWidth)
            }
            // FormHeader: separate groups clearly; the title sits close to
            // its group.
            if (item.trailing !== undefined) {
                if (item.topPadding === _headerDefaultTopPadding) {
                    item.topPadding = Qt.binding(() => Kirigami.Units.gridUnit * 1.25)
                }
                colorHeaderTitle(item)
            }
        }
    }

    // FormHeader's title label has no explicit color, so styles that color
    // labels from the palette (not Kirigami.Theme) draw it with the light
    // scheme's text color, unreadable on dark schemes. Bind it to the
    // header's theme text color.
    function colorHeaderTitle(header) {
        const pending = [header]
        while (pending.length > 0) {
            const children = pending.shift().children
            for (let i = 0; i < children.length; ++i) {
                const child = children[i]
                if (child.text !== undefined && child.color !== undefined && child.text === header.title) {
                    child.color = Qt.binding(() => header.Kirigami.Theme.textColor)
                    return
                }
                pending.push(child)
            }
        }
    }

    ColumnLayout {
        id: pageHeader

        // Narrow windows: groups span the full width, so inset the header.
        readonly property real sideMargin: page.width > page.maximumContentWidth ? 0 : Kirigami.Units.largeSpacing * 2

        Layout.fillWidth: true
        Layout.maximumWidth: page.maximumContentWidth
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Kirigami.Units.gridUnit * 1.5
        Layout.leftMargin: sideMargin
        Layout.rightMargin: sideMargin
        spacing: 0

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 1
            text: page.title
            wrapMode: Text.Wrap
            // Explicit, so the title reads as the page title in every style.
            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.6
            font.weight: Font.Bold
            color: Kirigami.Theme.textColor
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: text.length > 0
            text: page.subtitle
            wrapMode: Text.Wrap
            color: page.secondaryTextColor
        }

        Loader {
            id: stageLoader
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            active: page.stage !== null
            visible: active
            sourceComponent: page.stage
        }
    }
}
