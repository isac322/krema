# Shell modules
add_library(krema-shell-interface STATIC
    IProtocol.cpp
)
target_link_libraries(krema-shell-interface PUBLIC Qt6::Core Qt6::Gui)
target_include_directories(krema-shell-interface PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Automatic MOC for signals in IProtocol
set_target_properties(krema-shell-interface PROPERTIES AUTOMOC ON)

add_subdirectory(kwin)
add_subdirectory(hyprland)
add_subdirectory(tray)
