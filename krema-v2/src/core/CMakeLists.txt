# Core module
add_library(krema-core SHARED
    debug/KremaConsole.cpp
    attention/AttentionManager.cpp
    attention/MediaAttentionProvider.cpp
    attention/UnityAttentionProvider.cpp
    engine/BaseItem.cpp
    engine/BaseIsland.cpp
    engine/BasePanel.cpp
    engine/LayoutManager.cpp
    engine/IconAnalyzer.cpp
    tasks/ITaskProvider.cpp
    tasks/IdentityBridge.cpp
    tasks/WorkspaceController.cpp
    qml_registration.cpp
)

target_sources(krema-core PRIVATE
    KremaGlobals.hpp
    attention/AttentionManager.hpp
    attention/MediaAttentionProvider.hpp
    attention/UnityAttentionProvider.hpp
    engine/BaseItem.hpp
    engine/BaseIsland.hpp
    engine/BasePanel.hpp
    engine/LayoutManager.hpp
    engine/IconAnalyzer.hpp
    tasks/ITaskProvider.hpp
    tasks/IdentityBridge.hpp
)

add_library(Krema::Core ALIAS krema-core)

target_include_directories(krema-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(krema-core PRIVATE Qt6::Core Qt6::Gui Qt6::DBus Qt6::Qml krema-config)

target_compile_definitions(krema-core PUBLIC
    $<$<CONFIG:Debug>:KREMA_DEBUG>
)

include(GenerateExportHeader)
generate_export_header(krema-core
    EXPORT_MACRO_NAME KREMA_CORE_EXPORT
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/krema_core_export.h
)
target_include_directories(krema-core PUBLIC ${CMAKE_CURRENT_BINARY_DIR} $<TARGET_PROPERTY:krema-config,INCLUDE_DIRECTORIES>)
