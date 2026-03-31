# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors

find_package(Qt6 REQUIRED COMPONENTS Test)

# --- DockVisibilityController ---
add_executable(krema_qtest_visibility
    test_dockvisibilitycontroller.cpp
)
target_link_libraries(krema_qtest_visibility PRIVATE Qt6::Test krema_lib)
target_include_directories(krema_qtest_visibility PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME DockVisibility_AlwaysVisible COMMAND krema_qtest_visibility alwaysVisible_initiallyVisible alwaysVisible_staysVisibleOnUnhover alwaysVisible_toggleIsNoop)
add_test(NAME DockVisibility_AutoHide COMMAND krema_qtest_visibility autoHide_initiallyHidden autoHide_hoverShowsAfterDelay autoHide_unhoverHidesAfterDelay autoHide_hoverCancelsHideTimer autoHide_toggleVisibility)
add_test(NAME DockVisibility_InteractionLock COMMAND krema_qtest_visibility interactionLock_preventsHiding interactionLock_refCounting interactionLock_neverGoesNegative)
add_test(NAME DockVisibility_Keyboard COMMAND krema_qtest_visibility keyboardActive_preventsHiding keyboardActive_hidesOnDeactivate)
add_test(NAME DockVisibility_DodgeWindows COMMAND krema_qtest_visibility dodgeWindows_visibleWhenNoOverlap dodgeWindows_hidesOnOverlap dodgeWindows_showsWhenOverlapRemoved dodgeWindows_hoverOverridesOverlap dodgeWindows_interactionOverridesOverlap)
add_test(NAME DockVisibility_ModeSwitching COMMAND krema_qtest_visibility modeSwitchClearsTimers modeSignalEmitted visibleSignalEmitted platformVisibilityModeUpdated)

# --- BackgroundStyle ---
add_executable(krema_qtest_backgroundstyle
    test_backgroundstyle.cpp
)
target_link_libraries(krema_qtest_backgroundstyle PRIVATE Qt6::Test krema_lib)
target_include_directories(krema_qtest_backgroundstyle PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME BackgroundStyle COMMAND krema_qtest_backgroundstyle)

# --- ScreenSettings ---
add_executable(krema_qtest_screensettings
    test_screensettings.cpp
)
target_link_libraries(krema_qtest_screensettings PRIVATE Qt6::Test krema_lib)
target_include_directories(krema_qtest_screensettings PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/src)

add_test(NAME ScreenSettings COMMAND krema_qtest_screensettings)

# --- NotificationTracker ---
add_executable(krema_qtest_notificationtracker
    test_notificationtracker.cpp
)
target_link_libraries(krema_qtest_notificationtracker PRIVATE Qt6::Test krema_lib)
target_include_directories(krema_qtest_notificationtracker PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME NotificationTracker COMMAND krema_qtest_notificationtracker)

# --- DockModel ---
add_executable(krema_qtest_dockmodel
    test_dockmodel.cpp
)
target_link_libraries(krema_qtest_dockmodel PRIVATE Qt6::Test krema_lib)
target_include_directories(krema_qtest_dockmodel PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/src)

add_test(NAME DockModel COMMAND krema_qtest_dockmodel)

# --- DockActions ---
add_executable(krema_qtest_dockactions
    test_dockactions.cpp
)
target_link_libraries(krema_qtest_dockactions PRIVATE Qt6::Test krema_lib)
target_include_directories(krema_qtest_dockactions PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests/qtest
)

add_test(NAME DockActions COMMAND krema_qtest_dockactions)

# --- TaskIconProvider ---
add_executable(krema_qtest_taskiconprovider
    test_taskiconprovider.cpp
)
target_link_libraries(krema_qtest_taskiconprovider PRIVATE Qt6::Test krema_lib)
target_include_directories(krema_qtest_taskiconprovider PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME TaskIconProvider COMMAND krema_qtest_taskiconprovider)

# --- MockTasksSource ---
add_executable(krema_qtest_mocktaskssource
    test_mocktaskssource.cpp
)
target_link_libraries(krema_qtest_mocktaskssource PRIVATE Qt6::Test krema_lib)
target_include_directories(krema_qtest_mocktaskssource PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/tests/qtest
)

add_test(NAME MockTasksSource COMMAND krema_qtest_mocktaskssource)

# --- Environment ---
set_tests_properties(
    DockVisibility_AlwaysVisible
    DockVisibility_AutoHide
    DockVisibility_InteractionLock
    DockVisibility_Keyboard
    DockVisibility_DodgeWindows
    DockVisibility_ModeSwitching
    BackgroundStyle
    ScreenSettings
    NotificationTracker
    DockModel
    DockActions
    TaskIconProvider
    MockTasksSource
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
