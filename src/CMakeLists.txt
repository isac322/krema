# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

qt_add_resources(QML_RESOURCES qml/qml.qrc)

kconfig_add_kcfg_files(KREMA_KCFG_SRCS GENERATE_MOC config/krema.kcfgc)

add_library(krema_lib STATIC
    app/application.cpp
    app/application.h

    platform/dockplatform.h
    platform/dockplatformfactory.h
    platform/dockplatformfactory.cpp
    platform/waylanddockplatform.h
    platform/waylanddockplatform.cpp

    shell/dockshell.h
    shell/dockshell.cpp
    shell/dockview.h
    shell/dockview.cpp
    shell/dockvisibilitycontroller.h
    shell/dockvisibilitycontroller.cpp
    shell/previewcontroller.h
    shell/previewcontroller.cpp
    shell/dockwidget.h
    shell/dockwidget.cpp
    shell/settingswindow.h
    shell/settingswindow.cpp

    style/backgroundstyle.h
    style/backgroundstyle.cpp

    models/dockmodel.h
    models/dockmodel.cpp
    models/dockactions.h
    models/dockactions.cpp
    models/dockcontextmenu.h
    models/dockcontextmenu.cpp
    models/taskiconprovider.h

    utils/zoomcalculator.h
    utils/surfacegeometry.h
    utils/inputregion.h
    utils/inputregion.cpp

    ${KREMA_KCFG_SRCS}
    ${QML_RESOURCES}
)

qt6_add_shaders(krema_lib "acrylic_shaders"
    BATCHABLE PRECOMPILE OPTIMIZED
    PREFIX "/"
    FILES qml/shaders/acrylic_overlay.frag
          qml/shaders/outer_shadow.frag
)

target_link_libraries(krema_lib PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    Qt6::Quick
    Qt6::QuickControls2
    KF6::WindowSystem
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::I18n
    KF6::I18nQml
    KF6::GlobalAccel
    KF6::ColorScheme
    KF6::IconThemes
    KF6::Crash
    KF6::XmlGui
    KF6::Service
    LayerShellQt::Interface
    PW::LibTaskManager
    Wayland::Client
)

target_include_directories(krema_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(krema main.cpp)
target_link_libraries(krema PRIVATE krema_lib)

install(TARGETS krema ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# Generate .desktop file with resolved install paths for KWin Wayland protocol access
configure_file(
    com.bhyoo.krema.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/com.bhyoo.krema.desktop
    @ONLY
)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/com.bhyoo.krema.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)

install(
    FILES com.bhyoo.krema.metainfo.xml
    DESTINATION ${KDE_INSTALL_METAINFODIR}
)
