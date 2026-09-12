# UI Shaders module
add_library(krema-ui-shaders SHARED)

qt_add_shaders(krema-ui-shaders
    PREFIX "/org/krema/shaders"
    FILES
        "outer_shadow.frag"
)

target_link_libraries(krema-ui-shaders PRIVATE
    Qt6::Quick
)

set_target_properties(krema-ui-shaders PROPERTIES
    AUTOMOC ON
)
