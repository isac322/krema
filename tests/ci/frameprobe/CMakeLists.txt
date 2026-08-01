# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Frame-stepped CI capture hook. Only built when KREMA_TEST_HOOKS=ON so release
# packages never link Qt6::Test or carry the capture code.

# Widgets: the dock context menu is a native QMenu.
# Qml: the probe resolves the DockSettings singleton to drive settings changes.
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test Widgets Qml)

add_library(krema_frameprobe STATIC
    frameprobe.h
    frameprobe.cpp
)

target_link_libraries(krema_frameprobe PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Test
    Qt6::Widgets
)

target_include_directories(krema_frameprobe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(krema_frameprobe PUBLIC KREMA_TEST_HOOKS)
