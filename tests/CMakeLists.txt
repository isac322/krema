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

# CLI contract (issue #59): `krema --version` prints "krema <version>" and
# `krema --help`/`--author`/`--license` their text, exiting 0 without a
# display or a running instance. These run headless (display env unset), like
# a package build's smoke check.
add_test(NAME krema_cli_version
    COMMAND $<TARGET_FILE:krema> --version
)
add_test(NAME krema_cli_help
    COMMAND $<TARGET_FILE:krema> --help
)
add_test(NAME krema_cli_author
    COMMAND $<TARGET_FILE:krema> --author
)
add_test(NAME krema_cli_license
    COMMAND $<TARGET_FILE:krema> --license
)
set_tests_properties(krema_cli_version krema_cli_help krema_cli_author krema_cli_license
    PROPERTIES
        ENVIRONMENT_MODIFICATION "WAYLAND_DISPLAY=unset:;DISPLAY=unset:;QT_QPA_PLATFORM=unset:;XDG_RUNTIME_DIR=set:/tmp/krema-cli-test-runtime"
        TIMEOUT 60
)
set_tests_properties(krema_cli_version PROPERTIES
    PASS_REGULAR_EXPRESSION "^krema ${PROJECT_VERSION}"
)
set_tests_properties(krema_cli_help PROPERTIES
    PASS_REGULAR_EXPRESSION "A dock for KDE Plasma 6"
)
set_tests_properties(krema_cli_author PROPERTIES
    PASS_REGULAR_EXPRESSION "Byeonghoon Yoo"
)
set_tests_properties(krema_cli_license PROPERTIES
    PASS_REGULAR_EXPRESSION "GNU GENERAL PUBLIC LICENSE"
)

# In a private session (issue #59):
# - `krema --version` while the dock is already running must print the
#   version before KDBusService's single-instance check forwards it away; the
#   first instance is started with a Qt option (-platform wayland), which must
#   still be accepted, so this also covers launching the real dock.
# - An unknown option must fail with "Unknown option". It is checked after
#   QApplication has consumed Qt's own options, so it needs a display: headless
#   it aborts in QApplication like any GUI launch.
# - The running dock's desktop file name is com.bhyoo.krema, unless the
#   advertised --desktopfile option overrides it.
find_program(KWIN_WAYLAND_EXECUTABLE kwin_wayland)
find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
if(KWIN_WAYLAND_EXECUTABLE AND DBUS_RUN_SESSION_EXECUTABLE)
    add_test(NAME krema_cli_version_while_running
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh
            ${CMAKE_CURRENT_SOURCE_DIR}/version-while-running.sh $<TARGET_FILE:krema> "krema ${PROJECT_VERSION}"
    )
    add_test(NAME krema_cli_unknown_option
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh
            ${CMAKE_CURRENT_SOURCE_DIR}/cli-unknown-option.sh $<TARGET_FILE:krema>
    )
    add_test(NAME krema_cli_desktopfile_default
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh
            ${CMAKE_CURRENT_SOURCE_DIR}/desktop-file-name.sh $<TARGET_FILE:krema> com.bhyoo.krema
    )
    add_test(NAME krema_cli_desktopfile_option
        COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- ${CMAKE_SOURCE_DIR}/tests/run-with-kwin.sh
            ${CMAKE_CURRENT_SOURCE_DIR}/desktop-file-name.sh $<TARGET_FILE:krema> org.example.Test
            --desktopfile org.example.Test
    )
    set_tests_properties(krema_cli_version_while_running krema_cli_unknown_option
        krema_cli_desktopfile_default krema_cli_desktopfile_option
        PROPERTIES TIMEOUT 120)
endif()

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(kwin)
add_subdirectory(qml)

if(KREMA_E2E_TESTS)
    add_subdirectory(appium)
endif()
