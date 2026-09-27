# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Qt Quick Test component tests for src/qml. The real QML files are loaded
# from the source tree; C++ backends are replaced by mocks (see README.md).
# Run: ctest -R qml --output-on-failure

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS QuickTest)

add_executable(krema_qml_tests
    main.cpp
    qmltestmocks.cpp
    qmltestmocks.h
)

target_compile_definitions(krema_qml_tests PRIVATE
    KREMA_QML_MOCKS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/mocks"
)

target_link_libraries(krema_qml_tests PRIVATE
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickTest
    KF6::I18nQml
)

set(KREMA_QML_TEST_FILES
    tst_dockitem_zoom
    tst_dockitem_geometry
    tst_dockitem_indicators
    tst_dock_main
    tst_preview_popup
)

foreach(test_name IN LISTS KREMA_QML_TEST_FILES)
    add_test(NAME qml_${test_name}
        COMMAND krema_qml_tests -input ${CMAKE_CURRENT_SOURCE_DIR}/${test_name}.qml
    )
    set_tests_properties(qml_${test_name} PROPERTIES
        LABELS "qml"
        TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic;LANGUAGE=en_US;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-config;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/xdg-cache"
    )
endforeach()
