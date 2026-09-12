cmake_minimum_required(VERSION 3.16)
project(KremaDock LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enforce hidden visibility by default (Rule 11)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

set(QT_DEFAULT_MAJOR_VERSION 6)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick DBus ShaderTools)
find_package(KF6 REQUIRED COMPONENTS Config ConfigWidgets DBusAddons)

# Project Modules (Rule 11)
add_subdirectory(src/config)
add_subdirectory(src/core)
add_subdirectory(src/shell)
add_subdirectory(src/ui/qml)
add_subdirectory(src/ui/shaders)

# Main Executable
add_executable(krema src/Main.cpp)

target_link_libraries(krema PRIVATE
    Qt6::Quick
    Krema::Core
    Krema::Shell::KWin
    Krema::Shell::Hyprland
    krema-config
    krema-ui-qml
    krema-ui-shaders
    KF6::DBusAddons
)

install(TARGETS krema ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
