# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

add_executable(krema_unit_tests
    smoke_test.cpp
    test_zoom_calculator.cpp
    test_surface_geometry.cpp
    test_input_region.cpp
)

target_link_libraries(krema_unit_tests PRIVATE
    Catch2::Catch2WithMain
    krema_lib
)

target_include_directories(krema_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

catch_discover_tests(krema_unit_tests)
