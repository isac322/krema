// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.delegates as Delegates
import org.kde.kirigamiaddons.formcard as FormCard

// Settings window: a searchable sidebar of pages next to the selected page.
// Kirigami.AbstractApplicationWindow has no pageStack (Kirigami's
// ApplicationWindow PageRow intercepts input) but provides
// showPassiveNotification(), which FormCard's AboutPage calls. AT-SPI2
// accessibility works correctly — sidebar and content share one coordinate
// space.
//
// SettingsWindow (C++) creates one window per open, passing `defaultModule`
// as an initial property, calls openModule() to switch pages while open and
// destroys the window once it is closed.
Kirigami.AbstractApplicationWindow {
    id: root

    title: i18n("Settings")
    width: Kirigami.Units.gridUnit * 56
    height: Kirigami.Units.gridUnit * 40
    minimumWidth: Kirigami.Units.gridUnit * 36
    minimumHeight: Kirigami.Units.gridUnit * 24

    color: Kirigami.Theme.backgroundColor
    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false

    // Module selected when the window is created (initial property).
    property string defaultModule: ""
    // Module whose page is shown.
    property string currentModule: ""

    // Sidebar entries in display order. `keywords` are the setting names of
    // the page so the search field also finds pages by their settings.
    readonly property var modules: [
        {
            moduleId: "icons",
            text: i18n("Icons"),
            iconName: "preferences-desktop-icons",
            group: "",
            keywords: [i18n("Icon size"), i18n("Icon spacing"), i18n("Zoom factor"), i18n("Zoom style"), i18n("Zoom animation"),
                i18n("Icon size normalization"), i18n("Icon scale"), i18n("Active window icon opacity"),
                i18n("Inactive window and launcher icon opacity"), i18n("Minimized window icon opacity")]
        },
        {
            moduleId: "layout",
            text: i18n("Layout & Position"),
            iconName: "preferences-desktop-display",
            group: "",
            keywords: [i18n("Screen edge"), i18n("Floating"), i18n("Corner radius")]
        },
        {
            moduleId: "panelstyle",
            text: i18n("Panel Style"),
            iconName: "preferences-desktop-color",
            group: "",
            keywords: [i18n("Style"), i18n("Opacity"), i18n("Use system color"), i18n("Use accent color"), i18n("Tint color")]
        },
        {
            moduleId: "shadow",
            text: i18n("Shadow"),
            iconName: "preferences-desktop-effects",
            group: "",
            keywords: [i18n("Enable shadow"), i18n("Light X"), i18n("Light Y"), i18n("Light height"), i18n("Light radius"),
                i18n("Panel elevation"), i18n("Shadow intensity"), i18n("Shadow color")]
        },
        {
            moduleId: "animations",
            text: i18n("Animations & Badges"),
            iconName: "preferences-desktop-notification",
            group: "",
            keywords: [i18n("Attention animation"), i18n("Badge display")]
        },
        {
            moduleId: "behavior",
            text: i18n("Behavior"),
            iconName: "preferences-system",
            group: "",
            keywords: [i18n("Visibility mode"), i18n("Reserve screen space"), i18n("Only dodge active window"),
                i18n("Show delay (ms)"), i18n("Hide delay (ms)"), i18n("Separate pinned and running apps"),
                i18n("Single window click action"), i18n("Grouped window click action")]
        },
        {
            moduleId: "monitors",
            text: i18n("Monitors & Desktops"),
            iconName: "video-display",
            group: "",
            keywords: [i18n("Monitor mode"), i18n("Selected monitors"), i18n("Follow trigger"), i18n("Screen transition"),
                i18n("Virtual desktops"), i18n("Display mode"), i18n("Other desktop opacity")]
        },
        {
            moduleId: "preview",
            text: i18n("Window Preview"),
            iconName: "view-preview",
            group: "",
            keywords: [i18n("Show window previews on hover"), i18n("Thumbnail width (px)"), i18n("Hover delay (ms)"),
                i18n("Hide delay (ms)")]
        },
        {
            moduleId: "about",
            text: i18n("About Krema"),
            iconName: "help-about",
            group: i18nc("@title:group", "About"),
            keywords: [i18n("Version"), i18n("Authors"), i18n("License")]
        },
        {
            moduleId: "aboutkde",
            text: i18n("About KDE"),
            iconName: "kde",
            group: i18nc("@title:group", "About"),
            keywords: [i18n("KDE")]
        }
    ]

    readonly property var pageUrls: ({
        "icons": "settings/IconsPage.qml",
        "layout": "settings/LayoutPage.qml",
        "panelstyle": "settings/PanelStylePage.qml",
        "shadow": "settings/ShadowPage.qml",
        "animations": "settings/AnimationsPage.qml",
        "behavior": "settings/BehaviorPage.qml",
        "monitors": "settings/MonitorsPage.qml",
        "preview": "settings/PreviewPage.qml"
    })

    // Page components created on first use, keyed by module id.
    property var pageComponents: ({})

    // Entries matching the search text. The current page always stays listed
    // so the selection never disappears from the sidebar.
    readonly property var visibleModules: {
        const query = searchField.text.trim().toLowerCase();
        if (query.length === 0) {
            return modules;
        }
        return modules.filter(entry => entry.moduleId === currentModule
                              || entry.text.toLowerCase().includes(query)
                              || entry.keywords.some(keyword => keyword.toLowerCase().includes(query)));
    }

    // Unknown ids (e.g. the former "appearance" module) open the first page.
    function resolveModule(moduleId) {
        return modules.some(entry => entry.moduleId === moduleId) ? moduleId : "icons";
    }

    // Called by SettingsWindow to select a page, also while already open.
    function openModule(moduleId) {
        currentModule = resolveModule(moduleId);
    }

    function componentFor(moduleId) {
        if (moduleId === "about") {
            return aboutPageComponent;
        }
        if (moduleId === "aboutkde") {
            return aboutKdeComponent;
        }
        if (!pageComponents[moduleId]) {
            pageComponents[moduleId] = Qt.createComponent(Qt.resolvedUrl(pageUrls[moduleId]));
        }
        return pageComponents[moduleId];
    }

    function moduleIndex(moduleId) {
        return visibleModules.findIndex(entry => entry.moduleId === moduleId);
    }

    Component.onCompleted: openModule(defaultModule)

    // Same as kirigami-addons' ConfigWindow: Escape closes the window. Key
    // events not taken by the focused control propagate up to contentItem,
    // which also works while the window lacks shortcut focus.
    contentItem.Keys.onEscapePressed: root.close()

    Component {
        id: aboutPageComponent
        FormCard.AboutPage {}
    }
    Component {
        id: aboutKdeComponent
        FormCard.AboutKDE {}
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sidebar: search pill on top, entries with color icons, the
        // selected entry filled with the accent color.
        Rectangle {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 14
            Layout.fillHeight: true
            color: Kirigami.Theme.backgroundColor

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Kirigami.SearchField {
                    id: searchField
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.largeSpacing
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                    Accessible.name: i18n("Search settings")
                    // Filtering follows `text` live; only Return opens a
                    // match (the default auto-accept would switch pages
                    // while the user is still typing).
                    autoAccept: false

                    background: Rectangle {
                        radius: height / 2
                        color: Qt.alpha(Kirigami.Theme.textColor, searchField.hovered ? 0.1 : 0.07)
                        border.width: searchField.activeFocus ? 2 : 0
                        border.color: Kirigami.Theme.focusColor
                    }

                    KeyNavigation.down: sidebarList
                    KeyNavigation.tab: sidebarList
                    // Return opens the first match other than the current page.
                    onAccepted: {
                        const match = root.visibleModules.find(entry => entry.moduleId !== root.currentModule)
                        if (match && text.trim().length > 0) {
                            root.openModule(match.moduleId)
                        }
                    }
                }

                QQC2.ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    // The list follows the ScrollView's available width; the
                    // delegate already stops short of the side margins, so
                    // no horizontal scrollbar appears.
                    QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                    ListView {
                        id: sidebarList

                        clip: true
                        model: root.visibleModules
                        currentIndex: root.moduleIndex(root.currentModule)
                        activeFocusOnTab: true
                        keyNavigationEnabled: false
                        leftMargin: Kirigami.Units.smallSpacing
                        rightMargin: Kirigami.Units.smallSpacing
                        bottomMargin: Kirigami.Units.smallSpacing
                        Accessible.role: Accessible.List
                        Accessible.name: i18n("Settings pages")

                        function step(delta) {
                            const count = root.visibleModules.length
                            if (count === 0) {
                                return
                            }
                            const index = Math.max(0, Math.min(count - 1, currentIndex + delta))
                            root.openModule(root.visibleModules[index].moduleId)
                            positionViewAtIndex(index, ListView.Contain)
                        }

                        Keys.onUpPressed: event => {
                            if (currentIndex <= 0) {
                                searchField.forceActiveFocus()
                            } else {
                                step(-1)
                            }
                        }
                        Keys.onDownPressed: step(1)
                        Keys.onReturnPressed: contentLoader.forceActiveFocus()
                        Keys.onEnterPressed: contentLoader.forceActiveFocus()

                        delegate: ColumnLayout {
                            id: entryItem

                            required property var modelData
                            required property int index

                            readonly property bool startsGroup: modelData.group.length > 0
                                && (index === 0 || root.visibleModules[index - 1].group !== modelData.group)

                            width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
                            spacing: 0

                            // Group title (e.g. "About"), not a page.
                            QQC2.Label {
                                Layout.fillWidth: true
                                Layout.topMargin: Kirigami.Units.largeSpacing * 2
                                Layout.bottomMargin: Kirigami.Units.smallSpacing
                                Layout.leftMargin: Kirigami.Units.largeSpacing
                                visible: entryItem.startsGroup
                                text: entryItem.modelData.group
                                elide: Text.ElideRight
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                font.weight: Font.DemiBold
                                color: Kirigami.ColorUtils.linearInterpolation(
                                    Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.7)
                                Accessible.role: Accessible.Heading
                            }

                            Delegates.RoundedItemDelegate {
                                id: entryDelegate
                                Layout.fillWidth: true

                                // The selected entry uses the Selection colors:
                                // accent background, matching text color.
                                Kirigami.Theme.colorSet: checked ? Kirigami.Theme.Selection : Kirigami.Theme.Window
                                Kirigami.Theme.inherit: false

                                verticalPadding: Math.round(Kirigami.Units.smallSpacing / 2)
                                implicitHeight: Math.max(Math.round(Kirigami.Units.gridUnit * 1.5),
                                                         Kirigami.Units.iconSizes.smallMedium + 2 * verticalPadding)
                                    + topInset + bottomInset
                                icon.width: Kirigami.Units.iconSizes.smallMedium
                                icon.height: Kirigami.Units.iconSizes.smallMedium

                                text: entryItem.modelData.text
                                icon.name: entryItem.modelData.iconName
                                checkable: true
                                checked: entryItem.modelData.moduleId === root.currentModule
                                highlighted: checked
                                focusPolicy: Qt.NoFocus

                                Accessible.name: text
                                Accessible.description: i18n("Settings page: %1", text)

                                // Clicking the selected entry must keep it checked.
                                onToggled: checked = Qt.binding(() => entryItem.modelData.moduleId === root.currentModule)
                                onClicked: root.openModule(entryItem.modelData.moduleId)

                                // Keyboard focus on the list marks the selected entry.
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.leftMargin: entryDelegate.leftInset + 2
                                    anchors.rightMargin: entryDelegate.rightInset + 2
                                    anchors.topMargin: entryDelegate.topInset + 2
                                    anchors.bottomMargin: entryDelegate.bottomInset + 2
                                    visible: entryDelegate.checked && sidebarList.activeFocus
                                    radius: Kirigami.Units.cornerRadius
                                    color: Qt.alpha(Kirigami.Theme.backgroundColor, 0)
                                    border.width: 1
                                    border.color: Qt.alpha(Kirigami.Theme.textColor, 0.8)
                                }
                            }
                        }
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillHeight: true
        }

        // Content area
        Loader {
            id: contentLoader
            objectName: "settingsPageLoader"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            sourceComponent: root.currentModule.length > 0 ? root.componentFor(root.currentModule) : null
        }
    }
}
