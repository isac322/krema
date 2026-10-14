# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

# GUI integration tests: real dock shells on a private KWin virtual compositor.
# Tests use two outputs by default; selected-monitor coverage uses three.
find_program(KWIN_WAYLAND_EXECUTABLE kwin_wayland)
find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(krema_kwin_tests
    test_primary_output.cpp
)

# Catch2's fatal assertions must unwind scoped fixtures.
kde_target_enable_exceptions(krema_kwin_tests PRIVATE)

add_executable(krema_selected_output_tests
    test_selected_outputs.cpp
)

kde_target_enable_exceptions(krema_selected_output_tests PRIVATE)

target_link_libraries(krema_selected_output_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)

target_link_libraries(krema_kwin_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)

target_include_directories(krema_kwin_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

target_include_directories(krema_selected_output_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

# Grouped-task click/wheel actions. A separate binary because
# krema_kwin_tests forces the layer-shell integration process-wide, while this
# test spawns itself as an ordinary two-window app.
add_executable(krema_grouped_activation_tests
    test_grouped_activation.cpp
)

kde_target_enable_exceptions(krema_grouped_activation_tests PRIVATE)

target_link_libraries(krema_grouped_activation_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)
add_executable(krema_task_icons_tests
    test_task_icons.cpp
)

kde_target_enable_exceptions(krema_task_icons_tests PRIVATE)

target_link_libraries(krema_task_icons_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)

add_executable(krema_launch_in_place_tests
    test_launch_in_place.cpp
)

# Catch2 throws on REQUIRE failures; unwind fixture objects before the next case.
kde_target_enable_exceptions(krema_launch_in_place_tests PRIVATE)

target_link_libraries(krema_launch_in_place_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)


add_executable(krema_showdesktop_tests
    test_show_desktop.cpp
)

kde_target_enable_exceptions(krema_showdesktop_tests PRIVATE)

target_link_libraries(krema_showdesktop_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)

add_executable(krema_tooltip_tests
    test_tooltip_geometry.cpp
)

kde_target_enable_exceptions(krema_tooltip_tests PRIVATE)

target_link_libraries(krema_tooltip_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)

add_executable(krema_hittest_tests
    test_dock_hit_test.cpp
)

kde_target_enable_exceptions(krema_hittest_tests PRIVATE)

target_link_libraries(krema_hittest_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)

add_executable(krema_preview_tests
    test_preview_setting.cpp
)

kde_target_enable_exceptions(krema_preview_tests PRIVATE)

target_link_libraries(krema_preview_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)

add_executable(krema_delegate_geometry_tests
    test_delegate_geometry.cpp
)

kde_target_enable_exceptions(krema_delegate_geometry_tests PRIVATE)

target_link_libraries(krema_delegate_geometry_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)

# Floating gap measured from panels (issue #90). A separate binary because it
# spawns itself as a stand-in panel process (a pid other than Krema's).
add_executable(krema_panel_margin_tests
    test_panel_margin.cpp
)

kde_target_enable_exceptions(krema_panel_margin_tests PRIVATE)

target_link_libraries(krema_panel_margin_tests PRIVATE
    Catch2::Catch2
    Qt6::Test
    krema_lib
)

if(KWIN_WAYLAND_EXECUTABLE AND DBUS_RUN_SESSION_EXECUTABLE)
    add_test(NAME krema_kwin_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_kwin_tests>
    )
    add_test(NAME krema_selected_output_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_selected_output_tests>
    )
    set_tests_properties(krema_selected_output_tests PROPERTIES
        TIMEOUT 300
        ENVIRONMENT "KREMA_TEST_OUTPUT_COUNT=3"
    )

    set_tests_properties(krema_kwin_tests PROPERTIES TIMEOUT 300)

    add_test(NAME krema_grouped_activation_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_grouped_activation_tests>
    )
    set_tests_properties(krema_grouped_activation_tests PROPERTIES TIMEOUT 300)
    add_test(NAME krema_task_icons_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_task_icons_tests>
    )
    set_tests_properties(krema_task_icons_tests PROPERTIES TIMEOUT 300)
    add_test(NAME krema_launch_in_place_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_launch_in_place_tests>
    )
    set_tests_properties(krema_launch_in_place_tests PROPERTIES TIMEOUT 300)


    add_test(NAME krema_showdesktop_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_showdesktop_tests>
    )
    set_tests_properties(krema_showdesktop_tests PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "KREMA_TEST_KWIN_EFFECTS=${CMAKE_CURRENT_SOURCE_DIR}/effects"
    )

    add_test(NAME krema_tooltip_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_tooltip_tests>
    )
    set_tests_properties(krema_tooltip_tests PROPERTIES TIMEOUT 120)

    add_test(NAME krema_hittest_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_hittest_tests>
    )
    set_tests_properties(krema_hittest_tests PROPERTIES TIMEOUT 300)

    add_test(NAME krema_preview_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_preview_tests>
    )
    set_tests_properties(krema_preview_tests PROPERTIES TIMEOUT 120)

    add_test(NAME krema_delegate_geometry_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_delegate_geometry_tests>
    )
    set_tests_properties(krema_delegate_geometry_tests PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "KREMA_TEST_KWIN_EFFECTS=${CMAKE_CURRENT_SOURCE_DIR}/effects"
    )

    add_test(NAME krema_panel_margin_tests
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh $<TARGET_FILE:krema_panel_margin_tests>
    )
    set_tests_properties(krema_panel_margin_tests PROPERTIES TIMEOUT 300)
else()
    message(STATUS "kwin_wayland or dbus-run-session not found: krema_kwin_tests, krema_selected_output_tests, krema_grouped_activation_tests, krema_task_icons_tests, krema_launch_in_place_tests, krema_showdesktop_tests, krema_tooltip_tests, krema_hittest_tests, krema_preview_tests, krema_delegate_geometry_tests and krema_panel_margin_tests are built but not registered with CTest")
endif()
