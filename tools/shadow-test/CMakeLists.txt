cmake_minimum_required(VERSION 3.22)
project(shadow-test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 ShaderTools)

qt_add_executable(shadow-test main.cpp)

qt_add_qml_module(shadow-test
    URI ShadowTest
    VERSION 1.0
    QML_FILES main.qml
)

qt6_add_shaders(shadow-test "shaders"
    PREFIX "/shaders"
    FILES outer_shadow.frag
)

target_link_libraries(shadow-test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
)
