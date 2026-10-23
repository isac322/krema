# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

add_executable(krema_unit_tests
    smoke_test.cpp
    test_catch2_cleanup.cpp
    test_zoom_calculator.cpp
    test_surface_geometry.cpp
    test_input_region.cpp
    test_panel_inset.cpp
    test_output_order.cpp
    test_background_opacity.cpp
    test_settings_autosave.cpp
    test_settings_migration.cpp
    test_taskiconprovider.cpp
    test_desktop_actions.cpp
    test_trash.cpp
)

# Catch2's fatal assertions must unwind scoped fixtures.
kde_target_enable_exceptions(krema_unit_tests PRIVATE)

target_link_libraries(krema_unit_tests PRIVATE
    Catch2::Catch2WithMain
    krema_lib
)

target_include_directories(krema_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests
)

# The fatal-assertion regression needs both cases in the same process.
catch_discover_tests(krema_unit_tests TEST_SPEC "~[catch2-cleanup]")
add_test(NAME krema_catch2_cleanup
    COMMAND krema_unit_tests "[catch2-cleanup]" --order lex --rng-seed 1564808301
)
set_tests_properties(krema_catch2_cleanup PROPERTIES TIMEOUT 30)
