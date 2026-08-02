# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# Frame-stepped CI capture hook. Only built when KREMA_TEST_HOOKS=ON so release
# packages never link Qt6::Test or carry the capture code.

# Widgets: the dock context menu is a native QMenu.
# Qml: the probe resolves the DockSettings singleton to drive settings changes.
# CorePrivate carries QUnifiedTimer, whose consistent-timing mode is what
# makes an animation advance by the driver step instead of the real clock.
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test Widgets Qml CorePrivate)
# The probe derives window placement from the layer-shell anchors the client set.
find_package(LayerShellQt REQUIRED)

add_library(krema_frameprobe STATIC
    frameprobe.h
    frameprobe.cpp
)

target_link_libraries(krema_frameprobe PUBLIC
    Qt6::Core
    # QUnifiedTimer::setConsistentTiming is the only way to stop Qt measuring
    # each animation tick against the real clock, which is what makes a frame
    # number mean the same thing on every run. Test-only target, so the private
    # dependency never reaches a release package.
    Qt6::CorePrivate
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Test
    Qt6::Widgets
    LayerShellQt::Interface
)

target_include_directories(krema_frameprobe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(krema_frameprobe PUBLIC KREMA_TEST_HOOKS)
