// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import com.bhyoo.krema 1.0
import org.kde.kirigami as Kirigami

Item {
    id: root

    property bool isPinned: false
    property var configViewItem: root
    property int currentCategoryIndex: 0
    property int currentSubItemIndex: 0
    property var menuData: [{
        "name": i18n("Appearance"),
        "icon": "preferences-desktop-theme",
        "subItems": [{
            "id": "background",
            "name": i18n("Background & Blur"),
            "icon": "preferences-desktop-wallpaper",
            "page": "settings/BackgroundPage.qml"
        }, {
            "id": "shadow",
            "name": i18n("Shadow Effects"),
            "icon": "format-text-shadow",
            "page": "settings/ShadowPage.qml"
        }, {
            "id": "separator",
            "name": i18n("Separators"),
            "icon": "format-stroke-color",
            "page": "settings/SeparatorPage.qml"
        }]
    }, {
        "name": i18n("Dimensions"),
        "icon": "transform-scale",
        "subItems": [{
            "id": "panel",
            "name": i18n("Panel Geometry"),
            "icon": "measure",
            "page": "settings/PanelPage.qml"
        }, {
            "id": "icons",
            "name": i18n("Icon Sizing & Gaps"),
            "icon": "preferences-desktop-icons",
            "page": "settings/IconsPage.qml"
        }]
    }, {
        "name": i18n("Interaction"),
        "icon": "preferences-system",
        "subItems": [{
            "id": "visibility",
            "name": i18n("Visibility & Hiding"),
            "icon": "view-visible",
            "page": "settings/VisibilityPage.qml"
        }, {
            "id": "behavior",
            "name": i18n("Animations & Logic"),
            "icon": "preferences-system-windows",
            "page": "settings/BehaviorPage.qml"
        }, {
            "id": "preview",
            "name": i18n("Window Previews"),
            "icon": "view-preview",
            "page": "settings/PreviewPage.qml"
        }]
    }, {
        "name": i18n("Workspace"),
        "icon": "video-display",
        "subItems": [{
            "id": "multimonitor",
            "name": i18n("Screen Selection"),
            "icon": "video-display",
            "page": "settings/MonitorPage.qml"
        }, {
            "id": "virtualdesktops",
            "name": i18n("Virtual Desktops"),
            "icon": "preferences-desktop-virtual",
            "page": "settings/VirtualDesktopsPage.qml"
        }]
    }, {
        "id": "about",
        "name": i18n("About Krema"),
        "icon": "help-about",
        "page": "settings/AboutPage.qml"
    }]

    function open(module) {
        root.visible = true;
        if (!module)
            return ;

        var target = module.toString().toLowerCase();
        for (var i = 0; i < root.menuData.length; i++) {
            var cat = root.menuData[i];
            if (cat.id === target) {
                currentCategoryIndex = i;
                pageLoader.source = cat.page;
                return ;
            }
            if (cat.subItems) {
                for (var j = 0; j < cat.subItems.length; j++) {
                    if (cat.subItems[j].id === target) {
                        currentCategoryIndex = i;
                        currentSubItemIndex = j;
                        pageLoader.source = cat.subItems[j].page;
                        return ;
                    }
                }
            }
        }
    }

    objectName: "configuration"
    implicitWidth: 840
    implicitHeight: 660
    onVisibleChanged: {
        if (typeof DockVisibility !== "undefined")
            DockVisibility.liveEditMode = root.visible;

        if (!root.visible && typeof DockSettings !== "undefined")
            DockSettings.save();

    }

    // --- Dynamic Theme Engine ---
    QtObject {
        id: theme

        property bool isDark: DockSettings.settingsThemeMode === 1
        readonly property color base: isDark ? "#181B20" : "#EBE9E4"
        readonly property color sidebar: isDark ? "#111418" : "#D8DCE0"
        readonly property color card: isDark ? "#22262B" : "#FDFBFA"
        readonly property color accent: isDark ? "#7BA4B5" : "#5C7C8A"
        readonly property color text: isDark ? "#F9F7F2" : "#181C20"
        readonly property color textDim: isDark ? "#949DA6" : "#5C646B"
        readonly property color border: isDark ? "#2E343A" : "#CAD0D6"
    }

    // --- THE MAIN CHASSIS ---
    Rectangle {
        id: settingsChassis

        anchors.fill: parent
        color: theme.base
        // --- ADAPTIVE CORNER RADII (Qt 6.8+) ---
        // Top edge sharp
        topLeftRadius: (DockSettings.edge === 0 || DockSettings.edge === 2) ? 0 : 20
        topRightRadius: (DockSettings.edge === 0 || DockSettings.edge === 3) ? 0 : 20
        // Bottom edge sharp
        bottomLeftRadius: (DockSettings.edge === 1 || DockSettings.edge === 2) ? 0 : 20
        bottomRightRadius: (DockSettings.edge === 1 || DockSettings.edge === 3) ? 0 : 20
        border.color: theme.border
        border.width: 1
        clip: true

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // 1. SIDEBAR
            Rectangle {
                id: sidebarContainer

                Layout.fillHeight: true
                Layout.preferredWidth: 220
                color: theme.sidebar
                // Sidebar must match the rounded corners of the chassis
                topLeftRadius: settingsChassis.topLeftRadius
                bottomLeftRadius: settingsChassis.bottomLeftRadius
                topRightRadius: 0
                bottomRightRadius: 0

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 24

                    RowLayout {
                        Layout.leftMargin: 8
                        spacing: 12

                        Rectangle {
                            width: 32
                            height: 32
                            radius: 8
                            color: theme.text

                            Label {
                                anchors.centerIn: parent
                                text: "K"
                                color: theme.base
                                font.bold: true
                                font.pixelSize: 18
                            }

                        }

                        Label {
                            text: "Krema"
                            color: theme.text
                            font.bold: true
                            font.pixelSize: 20
                            font.letterSpacing: 1.2
                        }

                    }

                    ListView {
                        id: sidebarMenu

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: root.menuData
                        currentIndex: root.currentCategoryIndex
                        spacing: 8
                        interactive: false

                        delegate: Item {
                            property bool isSelected: index === root.currentCategoryIndex

                            width: ListView.view.width
                            height: 48

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 2
                                radius: 10
                                color: isSelected ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.3) : (sidebarMouse.containsMouse ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.1) : "transparent")

                                Behavior on color {
                                    ColorAnimation {
                                        duration: 150
                                    }

                                }

                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                spacing: 12

                                Kirigami.Icon {
                                    source: modelData.icon
                                    color: isSelected ? theme.text : theme.textDim
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 20
                                }

                                Label {
                                    text: modelData.name
                                    color: isSelected ? theme.text : theme.textDim
                                    font.weight: isSelected ? Font.DemiBold : Font.Normal
                                }

                            }

                            MouseArea {
                                id: sidebarMouse

                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.currentCategoryIndex = index;
                                    var cat = root.menuData[index];
                                    if (cat.subItems) {
                                        root.currentSubItemIndex = 0;
                                        pageLoader.source = cat.subItems[0].page;
                                    } else {
                                        pageLoader.source = cat.page;
                                    }
                                }
                            }

                        }

                    }

                    Label {
                        text: i18n("v0.8.0 Prototype")
                        color: theme.textDim
                        font.pixelSize: 10
                        Layout.alignment: Qt.AlignHCenter
                        Layout.bottomMargin: 8
                    }

                }

            }

            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                color: theme.border
            }

            // 2. CONTENT AREA
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // --- CONTENT BACKGROUND (matches right rounded corners) ---
                Rectangle {
                    anchors.fill: parent
                    color: theme.base
                    topRightRadius: settingsChassis.topRightRadius
                    bottomRightRadius: settingsChassis.bottomRightRadius
                    z: -1
                }

                // --- BULLETPROOF HEADER ---
                Rectangle {
                    id: headerArea

                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 64
                    color: "transparent"
                    z: 100

                    // DYNAMIC SUB-NAVIGATION BAR
                    ListView {
                        id: subTabBar

                        property bool hasSubs: root.menuData[root.currentCategoryIndex].subItems !== undefined

                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: windowControls.left
                        anchors.leftMargin: 40
                        anchors.rightMargin: 16
                        orientation: ListView.Horizontal
                        model: hasSubs ? root.menuData[root.currentCategoryIndex].subItems : []
                        currentIndex: root.currentSubItemIndex
                        spacing: 8
                        clip: true
                        opacity: hasSubs ? 1 : 0

                        Behavior on opacity {
                            OpacityAnimator {
                                duration: 200
                            }

                        }

                        delegate: Item {
                            property bool isSelected: index === root.currentSubItemIndex

                            width: subContent.width + 32
                            height: 64

                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width
                                height: 32
                                radius: 16
                                color: isSelected ? theme.text : (subMouse.containsMouse ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12) : "transparent")

                                Behavior on color {
                                    ColorAnimation {
                                        duration: 150
                                    }

                                }

                            }

                            RowLayout {
                                id: subContent

                                anchors.centerIn: parent
                                spacing: 8

                                Label {
                                    text: modelData.name
                                    color: isSelected ? theme.card : theme.text
                                    font.weight: isSelected ? Font.DemiBold : Font.Normal
                                    font.pixelSize: 13
                                }

                            }

                            MouseArea {
                                id: subMouse

                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.currentSubItemIndex = index;
                                    pageLoader.source = modelData.page;
                                }
                            }

                        }

                    }

                    // ABSOLUTE WINDOW CONTROLS (Always Visible)
                    RowLayout {
                        id: windowControls

                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        height: 40
                        spacing: 8

                        QQC2.ToolButton {
                            id: pinBtn

                            width: 36
                            height: 36
                            ToolTip.visible: hovered
                            ToolTip.text: root.isPinned ? i18n("Unpin from top") : i18n("Keep Always on Top")
                            onClicked: root.isPinned = !root.isPinned

                            contentItem: Kirigami.Icon {
                                source: root.isPinned ? "window-pin" : "window-unpin"
                                color: root.isPinned ? theme.accent : (pinBtn.hovered ? theme.text : theme.textDim)
                            }

                            background: Rectangle {
                                radius: 8
                                color: pinBtn.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12) : "transparent"

                                Behavior on color {
                                    ColorAnimation {
                                        duration: 150
                                    }

                                }

                            }

                        }

                        QQC2.ToolButton {
                            id: closeBtn

                            width: 36
                            height: 36
                            onClicked: SettingsController.visible = false

                            contentItem: Item {
                                Kirigami.Icon {
                                    anchors.fill: parent
                                    source: "window-close"
                                    color: closeBtn.hovered ? "#D13438" : theme.textDim
                                }

                            }

                            background: Rectangle {
                                radius: 8
                                color: closeBtn.hovered ? Qt.rgba(209, 52, 56, 0.1) : "transparent"

                                Behavior on color {
                                    ColorAnimation {
                                        duration: 150
                                    }

                                }

                            }

                        }

                    }

                }

                Rectangle {
                    id: headerDivider

                    anchors.top: parent.top
                    anchors.topMargin: 64
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 24
                    anchors.rightMargin: 24
                    height: 1
                    color: theme.border
                }

                Loader {
                    id: pageLoader

                    anchors.top: headerDivider.bottom
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 16
                    source: "settings/BackgroundPage.qml"
                }

            }

        }

    }

}
