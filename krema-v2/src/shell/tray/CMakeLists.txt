find_package(KF6StatusNotifierItem REQUIRED)

add_library(krema-shell-tray SHARED
    TrayProvider.cpp
)

target_link_libraries(krema-shell-tray PUBLIC
    krema-shell-interface
    KF6::StatusNotifierItem
    Qt6::Core
)

include(GenerateExportHeader)
generate_export_header(krema-shell-tray
    EXPORT_MACRO_NAME KREMA_SHELL_TRAY_EXPORT
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/krema_shell_tray_export.h
)
target_include_directories(krema-shell-tray PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
