# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

add_executable(krema_unit_tests
    smoke_test.cpp
)

target_link_libraries(krema_unit_tests PRIVATE
    Catch2::Catch2WithMain
)

catch_discover_tests(krema_unit_tests)
