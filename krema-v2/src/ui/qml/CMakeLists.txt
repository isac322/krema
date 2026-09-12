# UI QML module
qt_add_qml_module(krema-ui-qml
    URI "org.krema.ui"
    VERSION 1.0
    QML_FILES
        AttentionOverlay.qml
        DockAnimator.qml
        ItemCapsule.qml
        IslandModule.qml
        MainDock.qml
        VisualPanel.qml
        VolumetricShadow.qml
        WorkspaceSelector.qml
        SettingsWindow.qml
)

target_link_libraries(krema-ui-qml PRIVATE
    Qt6::Quick
    Krema::Core
)
