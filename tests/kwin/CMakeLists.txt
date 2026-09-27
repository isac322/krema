# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# GUI integration tests: real dock shells on a private KWin virtual
# compositor with two outputs (see tests/run-with-kwin.sh).
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

# Grouped-task click/wheel actions. A separate binary because
# krema_kwin_tests forces the layer-shell integration process-wide, while this
# test spawns itself as an ordinary two-window app.
add_executable(krema_grouped_activation_tests
    test_grouped_activation.cpp
)

target_link_libraries(krema_grouped_activation_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)

add_executable(krema_showdesktop_tests
    test_show_desktop.cpp
)

target_link_libraries(krema_showdesktop_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)

add_executable(krema_hittest_tests
    test_dock_hit_test.cpp
)

target_link_libraries(krema_hittest_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)

if(KWIN_WAYLAND_EXECUTABLE AND DBUS_RUN_SESSION_EXECUTABLE)
    add_test(NAME krema_kwin_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_kwin_tests>
    )
    set_tests_properties(krema_kwin_tests PROPERTIES TIMEOUT 300)

    add_test(NAME krema_grouped_activation_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_grouped_activation_tests>
    )
    set_tests_properties(krema_grouped_activation_tests PROPERTIES TIMEOUT 300)

    add_test(NAME krema_showdesktop_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_showdesktop_tests>
    )
    set_tests_properties(krema_showdesktop_tests PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "KREMA_TEST_KWIN_EFFECTS=${CMAKE_CURRENT_SOURCE_DIR}/effects"
    )

    add_test(NAME krema_hittest_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_hittest_tests>
    )
    set_tests_properties(krema_hittest_tests PROPERTIES TIMEOUT 300)
else()
    message(STATUS "kwin_wayland or dbus-run-session not found: krema_kwin_tests, krema_grouped_activation_tests, krema_showdesktop_tests and krema_hittest_tests are built but not registered with CTest")
endif()
