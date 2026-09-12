# KWin Shell module (Layer-Shell)
find_package(LayerShellQt REQUIRED)
find_package(LibTaskManager REQUIRED)

add_library(krema-shell-kwin SHARED
    KWinProtocol.cpp
    KWinTaskProvider.cpp
)

add_library(Krema::Shell::KWin ALIAS krema-shell-kwin)

target_sources(krema-shell-kwin PRIVATE
    KWinProtocol.hpp
    KWinTaskProvider.hpp
)

target_link_libraries(krema-shell-kwin 
    PUBLIC 
        krema-shell-interface
        LayerShellQt::Interface
    PRIVATE 
        Qt6::Gui
        Qt6::Qml
        Krema::Core
        PW::LibTaskManager
)

include(GenerateExportHeader)
generate_export_header(krema-shell-kwin
    EXPORT_MACRO_NAME KREMA_SHELL_KWIN_EXPORT
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/krema_shell_kwin_export.h
)
target_include_directories(krema-shell-kwin PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
