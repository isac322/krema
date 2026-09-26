# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# GUI integration tests: real dock shells and settings QML on a private
# KWin virtual compositor (see run-with-kwin.sh).
find_program(KWIN_WAYLAND_EXECUTABLE kwin_wayland)
find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(krema_integration_tests
    test_settings_lifecycle.cpp
)

target_link_libraries(krema_integration_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)

if(KWIN_WAYLAND_EXECUTABLE AND DBUS_RUN_SESSION_EXECUTABLE)
    add_test(NAME krema_integration_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_CURRENT_SOURCE_DIR}/run-with-kwin.sh $<TARGET_FILE:krema_integration_tests>
    )
    set_tests_properties(krema_integration_tests PROPERTIES TIMEOUT 180)
else()
    message(STATUS "kwin_wayland or dbus-run-session not found: krema_integration_tests is built but not registered with CTest")
endif()
