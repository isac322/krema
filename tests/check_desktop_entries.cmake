# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Install-contract check for .desktop entries (issue #18, claim a):
# a desktop file that declares DBusActivatable=true is only launchable through
# KIO/Kickoff/KRunner when a matching D-Bus activation service file
# (dbus-1/services/<desktop-id>.service with Name= + Exec=) is installed.
# Krema ships no service files, so no shipped desktop entry may set the key.
#
# Runs in pure script mode over the source templates and the configured build
# products, so it works on a bare `cmake --build && ctest` without an install
# step:
#   cmake -DSOURCE_DIR=<src> -DBINARY_DIR=<build>/src -P check_desktop_entries.cmake

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "SOURCE_DIR and BINARY_DIR must be set")
endif()

file(GLOB desktop_templates "${SOURCE_DIR}/src/*.desktop.in")
file(GLOB desktop_built "${BINARY_DIR}/*.desktop")
file(GLOB_RECURSE service_files
    "${SOURCE_DIR}/src/*.service.in"
    "${BINARY_DIR}/*.service"
    "${SOURCE_DIR}/dbus-1/services/*"
)

set(failures "")
foreach(entry IN LISTS desktop_templates desktop_built)
    file(READ "${entry}" content)
    if(content MATCHES "DBusActivatable[ \t]*=[ \t]*true")
        get_filename_component(desktop_id "${entry}" NAME_WE)
        set(have_service FALSE)
        foreach(service IN LISTS service_files)
            file(READ "${service}" service_content)
            if(service_content MATCHES "Name[ \t]*=[ \t]*${desktop_id}")
                set(have_service TRUE)
            endif()
        endforeach()
        if(NOT have_service)
            list(APPEND failures
                "${entry}: DBusActivatable=true without a matching dbus-1/services/${desktop_id}.service")
        endif()
    endif()
endforeach()

if(failures)
    string(REPLACE ";" "\n  " msg "${failures}")
    message(FATAL_ERROR "Desktop entry D-Bus activation contract violated:\n  ${msg}")
endif()
message(STATUS "Desktop entry D-Bus activation contract OK")
