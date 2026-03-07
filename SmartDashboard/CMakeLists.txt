cmake_minimum_required(VERSION 3.24)

project(SmartDashboard LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_add_executable(SmartDashboardApp WIN32
    src/main.cpp
    src/app/main_window.h
    src/app/main_window.cpp
    src/layout/layout_serializer.h
    src/layout/layout_serializer.cpp
    src/transport/direct_subscriber_adapter.h
    src/transport/direct_subscriber_adapter.cpp
    src/widgets/variable_tile.h
    src/widgets/variable_tile.cpp
)

target_include_directories(SmartDashboardApp
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/SmartDashboard_Interface_direct/include
)

target_link_libraries(SmartDashboardApp
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        SmartDashboard_Interface_direct
)

target_compile_features(SmartDashboardApp PRIVATE cxx_std_20)
