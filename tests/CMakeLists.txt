# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

find_package(Catch2 3 REQUIRED)
include(Catch)

add_test(NAME krema_desktop_entries
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DBINARY_DIR=${CMAKE_BINARY_DIR}/src
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_desktop_entries.cmake
)

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(kwin)
add_subdirectory(qml)

if(KREMA_E2E_TESTS)
    add_subdirectory(appium)
endif()
