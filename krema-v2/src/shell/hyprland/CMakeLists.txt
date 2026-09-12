# Hyprland Shell module
find_package(LayerShellQt REQUIRED)

add_library(krema-shell-hyprland SHARED
    HyprlandProtocol.cpp
    HyprlandTaskProvider.cpp
)

add_library(Krema::Shell::Hyprland ALIAS krema-shell-hyprland)

target_sources(krema-shell-hyprland PRIVATE
    HyprlandProtocol.hpp
    HyprlandTaskProvider.hpp
)

target_link_libraries(krema-shell-hyprland 
    PUBLIC 
        krema-shell-interface
        LayerShellQt::Interface
    PRIVATE 
        Qt6::Gui
        Qt6::Qml
        Krema::Core
)

include(GenerateExportHeader)
generate_export_header(krema-shell-hyprland
    EXPORT_MACRO_NAME KREMA_SHELL_HYPRLAND_EXPORT
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/krema_shell_hyprland_export.h
)
target_include_directories(krema-shell-hyprland PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
