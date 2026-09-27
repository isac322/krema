# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Built into the E2E image (tests/appium/Dockerfile), not part of the krema build.

cmake_minimum_required(VERSION 3.22)
project(krema-test-window LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets DBus)

add_executable(krema-test-window main.cpp)
target_link_libraries(krema-test-window Qt6::Widgets Qt6::DBus)

install(TARGETS krema-test-window DESTINATION bin)
install(FILES org.kde.krema.testwindow.desktop org.kde.krema.testwindow2.desktop DESTINATION share/applications)
