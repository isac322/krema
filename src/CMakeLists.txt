# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

add_library(krema_lib STATIC
    app/application.cpp
    app/application.h
)

target_link_libraries(krema_lib PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    Qt6::Quick
    KF6::WindowSystem
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::I18n
    LayerShellQt::Interface
    Wayland::Client
)

target_include_directories(krema_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(krema main.cpp)
target_link_libraries(krema PRIVATE krema_lib)

install(TARGETS krema ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
