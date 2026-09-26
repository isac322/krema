# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# GUI integration tests: real dock shells on a private KWin virtual
# compositor with two outputs (see run-with-kwin.sh).
find_program(KWIN_WAYLAND_EXECUTABLE kwin_wayland)
find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(krema_kwin_tests
    test_primary_output.cpp
)

target_link_libraries(krema_kwin_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)

target_include_directories(krema_kwin_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

if(KWIN_WAYLAND_EXECUTABLE AND DBUS_RUN_SESSION_EXECUTABLE)
    add_test(NAME krema_kwin_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_CURRENT_SOURCE_DIR}/run-with-kwin.sh $<TARGET_FILE:krema_kwin_tests>
    )
    set_tests_properties(krema_kwin_tests PROPERTIES TIMEOUT 300)
else()
    message(STATUS "kwin_wayland or dbus-run-session not found: krema_kwin_tests is built but not registered with CTest")
endif()
