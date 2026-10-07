# SPDX-License-Identifier: GPL-3.0-or-later
# Local, AI-authored packaging glue; upstream library sources remain unchanged.
cmake_minimum_required(VERSION 3.22)
project(plasma-workspace VERSION 6.7.5 LANGUAGES C CXX)

set(QT_MIN_VERSION "6.10.0")
set(KF6_MIN_VERSION "6.26.0")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS ON)
set(BUILD_TESTING OFF CACHE BOOL "Build upstream tests" FORCE)

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)
include(ECMQtDeclareLoggingCategory)
include(ECMGenerateExportHeader)
include(ECMQmlModule)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
    Core Gui Quick Concurrent DBus WaylandClient)
find_package(Qt6GuiPrivate ${Qt6Gui_VERSION} EXACT REQUIRED NO_MODULE)
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons Config I18n ItemModels JobWidgets KIO Notifications
    Package Service WindowSystem)
find_package(Plasma 6.7.0 REQUIRED)
find_package(PlasmaActivities 6.7.0 REQUIRED)
find_package(KF6Screen 6.7.0 REQUIRED)
find_package(PlasmaWaylandProtocols 1.21.0 REQUIRED)
find_package(Canberra REQUIRED)
find_package(PkgConfig REQUIRED)
# Retain upstream's optional libflatpak support for notification resource paths.
pkg_check_modules(Flatpak IMPORTED_TARGET flatpak)

set(plasma-workspace_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/upstream")
set(SCREENSAVER_DBUS_INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/kscreenlocker/dbus/org.freedesktop.ScreenSaver.xml")
# Krema and this package target Wayland; no X11 session or X11-only models.
set(HAVE_X11 OFF)
configure_file("${plasma-workspace_SOURCE_DIR}/config-X11.h.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/config-X11.h")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")
add_definitions(-DQT_NO_URL_CAST_FROM_STRING)

# Use the original upstream target definitions, public headers, QML plugins,
# generated D-Bus bindings, package exports, and notification configuration.
add_subdirectory("${plasma-workspace_SOURCE_DIR}/libkworkspace" libkworkspace)
add_subdirectory("${plasma-workspace_SOURCE_DIR}/libtaskmanager" libtaskmanager)
add_subdirectory("${plasma-workspace_SOURCE_DIR}/libnotificationmanager" libnotificationmanager)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
