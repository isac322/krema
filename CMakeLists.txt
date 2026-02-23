# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

cmake_minimum_required(VERSION 3.22)

project(krema VERSION 0.3.0 LANGUAGES CXX)

set(QT_MIN_VERSION "6.6.0")
set(KF6_MIN_VERSION "6.0.0")
set(LAYER_SHELL_QT_MIN_VERSION "6.0.0")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(KDEClangFormat)
include(KDEGitCommitHooks)
include(ECMDeprecationSettings)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    DBus
    Quick
    QuickControls2
    ShaderTools
)
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    WindowSystem
    Config
    CoreAddons
    I18n
    GlobalAccel
    ColorScheme
    IconThemes
    Crash
    XmlGui
    Service
)
find_package(KF6Kirigami ${KF6_MIN_VERSION} REQUIRED)
find_package(KF6KirigamiAddons REQUIRED)
find_package(LayerShellQt ${LAYER_SHELL_QT_MIN_VERSION} REQUIRED)
find_package(LibTaskManager REQUIRED)
find_package(KPipeWire REQUIRED)
find_package(Wayland 1.22 REQUIRED COMPONENTS Client)

ecm_set_disabled_deprecation_versions(
    QT ${QT_MIN_VERSION}
    KF ${KF6_MIN_VERSION}
)

add_compile_definitions(KREMA_VERSION_STRING="${PROJECT_VERSION}")

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES src/*.cpp src/*.h)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})
kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
