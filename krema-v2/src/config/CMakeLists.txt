find_package(KF6Config REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)

kconfig_add_kcfg_files(KREMA_CONFIG_SRCS KremaSettings.kcfgc)

add_library(krema-config STATIC ${KREMA_CONFIG_SRCS})

# Explicitly tell Qt to process the generated C++ file for MOC
qt6_wrap_cpp(KREMA_CONFIG_MOC_SRCS TARGET krema-config ${KREMA_CONFIG_SRCS})
target_sources(krema-config PRIVATE ${KREMA_CONFIG_MOC_SRCS})

target_link_libraries(krema-config PUBLIC
    KF6::ConfigCore
    KF6::ConfigGui
    Qt6::Core
)

target_include_directories(krema-config PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
