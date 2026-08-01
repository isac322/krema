# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

cmake_minimum_required(VERSION 3.22)
project(krema-fixture-window LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)

add_executable(krema-fixture-window main.cpp)
target_link_libraries(krema-fixture-window PRIVATE Qt6::Core Qt6::Gui)
