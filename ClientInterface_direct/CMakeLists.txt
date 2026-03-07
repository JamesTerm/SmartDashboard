cmake_minimum_required(VERSION 3.24)

project(ClientInterface_direct LANGUAGES CXX)

add_library(ClientInterface_direct STATIC
    src/sd_direct_publisher_stub.cpp
)

target_include_directories(ClientInterface_direct
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/SmartDashboard_Interface_direct/include
)

target_compile_features(ClientInterface_direct PUBLIC cxx_std_20)

target_link_libraries(ClientInterface_direct
    PUBLIC
        SmartDashboard_DirectCommon
)

add_executable(sd_direct_publisher_sample
    samples/publisher_sample.cpp
)

target_link_libraries(sd_direct_publisher_sample
    PRIVATE
        ClientInterface_direct
)
