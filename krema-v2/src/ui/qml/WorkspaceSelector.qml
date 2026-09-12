import QtQuick
import org.krema.ui
import org.kde.kirigami as Kirigami

/**
 * @brief Workspace Selector component for filtering tasks.
 * Interacts with WorkspaceController to dynamically update active workspace.
 */
Item {
    id: root
    width: childrenRect.width
    height: 48

    Row {
        spacing: 10
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 10

        // Layout Mode Toggle (M7)
        Rectangle {
            width: 80
            height: 32
            radius: 4
            color: Kirigami.Theme.backgroundColor
            Text {
                anchors.centerIn: parent
                text: kremaSettings.layoutMode === 0 ? "Labels: Off" : "Labels: On"
                color: Kirigami.Theme.textColor
            }
            MouseArea {
                anchors.fill: parent
                onClicked: kremaSettings.layoutMode = (kremaSettings.layoutMode === 0 ? 1 : 0)
            }
        }

        // Workspace Selector
        Repeater {
            model: ["1", "2", "3"]
            delegate: Rectangle {
                width: 32
                height: 32
                radius: 4
                color: workspaceController.activeWorkspace === modelData ? Kirigami.Theme.highlightColor : Kirigami.Theme.backgroundColor
                
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    color: Kirigami.Theme.textColor
                }
                
                MouseArea {
                    anchors.fill: parent
                    onClicked: workspaceController.activeWorkspace = modelData
                }
            }
        }
    }
}
