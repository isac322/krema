# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

find_package(Catch2 3 REQUIRED)
include(Catch)

add_subdirectory(unit)

# ==========================================
# Icon Sandbox (Visual Testing)
# ==========================================
add_executable(krema_icon_sandbox sandbox_main.cpp)

# Include src/ so it can find your models/taskiconprovider.h
target_include_directories(krema_icon_sandbox PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Link against Qt6 and your internal library
target_link_libraries(krema_icon_sandbox PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    krema_lib
)
