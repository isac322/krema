// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.delegates as Delegates
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

// Custom settings window with plain sidebar (no OverlayDrawer).
// Uses QQC2.ApplicationWindow (not Kirigami.ApplicationWindow) to avoid
// pageStack input interception. AT-SPI2 accessibility works correctly —
// sidebar and content share one coordinate space.
QQC2.ApplicationWindow {
    id: root

    title: i18n("Settings") + " — Krema"
    width: Kirigami.Units.gridUnit * 50
    height: Kirigami.Units.gridUnit * 30
    minimumWidth: Kirigami.Units.gridUnit * 35
    minimumHeight: Kirigami.Units.gridUnit * 20

    property string defaultModule: ""
    property var pageCache: ({})

    color: Kirigami.Theme.backgroundColor
    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false

    ListModel {
        id: modulesModel
        ListElement { moduleId: "icons"; label: "Icons"; iconName: "preferences-desktop-icons" }
        ListElement { moduleId: "layout"; label: "Layout & Position"; iconName: "preferences-desktop-display" }
        ListElement { moduleId: "panelstyle"; label: "Panel Style"; iconName: "preferences-desktop-color" }
        ListElement { moduleId: "shadow"; label: "Shadow"; iconName: "preferences-desktop-effects" }
        ListElement { moduleId: "animations"; label: "Animations & Badges"; iconName: "preferences-desktop-notification" }
        ListElement { moduleId: "behavior"; label: "Behavior"; iconName: "preferences-system" }
        ListElement { moduleId: "preview"; label: "Window Preview"; iconName: "view-preview" }
        ListElement { moduleId: "about"; label: "About Krema"; iconName: "help-about" }
        ListElement { moduleId: "aboutkde"; label: "About KDE"; iconName: "kde" }
    }

    property var moduleUrls: ({
        "icons": "settings/IconsPage.qml",
        "layout": "settings/LayoutPage.qml",
        "panelstyle": "settings/PanelStylePage.qml",
        "shadow": "settings/ShadowPage.qml",
        "animations": "settings/AnimationsPage.qml",
        "behavior": "settings/BehaviorPage.qml",
        "preview": "settings/PreviewPage.qml",
    })

    function switchToModule(moduleId) {
        if (moduleId === "about") {
            contentLoader.setSource("", {})
            contentLoader.sourceComponent = aboutPageComponent
        } else if (moduleId === "aboutkde") {
            contentLoader.setSource("", {})
            contentLoader.sourceComponent = aboutKdeComponent
        } else {
            let url = moduleUrls[moduleId]
            if (!url) return
            contentLoader.setSource(Qt.resolvedUrl(url))
        }
    }

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

        // Sidebar
        Rectangle {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 13
            Layout.fillHeight: true
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
            color: Kirigami.Theme.backgroundColor

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Kirigami.SearchField {
                    id: searchField
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    onTextChanged: filterModel()

                    function filterModel() {
                        let text = searchField.text.toLowerCase()
                        for (let i = 0; i < modulesModel.count; i++) {
                            // Use visible flag via proxy
                        }
                        sidebarList.filterText = text
                    }
                }

                ListView {
                    id: sidebarList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    currentIndex: 0

                    property string filterText: ""

                    model: modulesModel

                    delegate: Delegates.RoundedItemDelegate {
                        id: sidebarDelegate

                        required property int index
                        required property string moduleId
                        required property string label
                        required property string iconName

                        visible: sidebarList.filterText.length === 0
                                 || label.toLowerCase().includes(sidebarList.filterText)
                        height: visible ? implicitHeight : 0

                        width: ListView.view.width
                        text: i18n(label)
                        icon.name: iconName
                        highlighted: ListView.isCurrentItem

                        Accessible.role: Accessible.ListItem
                        Accessible.name: text
                        Accessible.description: i18n("Settings page: %1", text)

                        onClicked: {
                            ListView.view.currentIndex = index
                            root.switchToModule(moduleId)
                        }
                    }

                    Component.onCompleted: {
                        let target = root.defaultModule || "icons"
                        for (let i = 0; i < modulesModel.count; i++) {
                            if (modulesModel.get(i).moduleId === target) {
                                currentIndex = i
                                break
                            }
                        }
                        root.switchToModule(target)
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
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
        }
    }
}
