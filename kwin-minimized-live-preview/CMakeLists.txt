cmake_minimum_required(VERSION 3.20)
project(KWinMinimizedLivePreview LANGUAGES CXX)

# KWin loads effects as dynamic plugin modules.
set(BUILD_SHARED_LIBS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

find_package(ECM 6.0 REQUIRED NO_MODULE)
list(PREPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")
set(QT_MAJOR_VERSION 6)
include(KDEInstallDirs6)

find_package(KF6CoreAddons REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(KWin REQUIRED)

kcoreaddons_add_plugin(kwin4_effect_minimized_live_preview
    SOURCES minimizedlivepreview.cpp
    INSTALL_NAMESPACE "kwin/effects/plugins"
)

target_link_libraries(kwin4_effect_minimized_live_preview
    PRIVATE
        KWin::kwin
        Qt6::Core
)
