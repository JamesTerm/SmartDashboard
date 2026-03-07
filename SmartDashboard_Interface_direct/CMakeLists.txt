cmake_minimum_required(VERSION 3.24)

project(SmartDashboard_Interface_direct LANGUAGES CXX)

add_library(SmartDashboard_Interface_direct STATIC
    src/sd_direct_subscriber_stub.cpp
)

target_include_directories(SmartDashboard_Interface_direct
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(SmartDashboard_Interface_direct PUBLIC cxx_std_20)
