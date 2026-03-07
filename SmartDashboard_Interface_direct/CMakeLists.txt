cmake_minimum_required(VERSION 3.24)

project(SmartDashboard_Interface_direct LANGUAGES CXX)

set(DIRECT_COMMON_HEADERS
    include/sd_direct_clock.h
    include/sd_direct_shared_memory.h
    include/sd_direct_ring.h
    include/sd_direct_wire.h
)

set(DIRECT_PUBLIC_HEADERS
    include/sd_direct_types.h
    include/sd_direct_subscriber.h
)

add_library(SmartDashboard_DirectCommon STATIC
    ${DIRECT_COMMON_HEADERS}
    src/sd_direct_clock.cpp
    src/sd_direct_shared_memory.cpp
    src/sd_direct_ring.cpp
)

target_include_directories(SmartDashboard_DirectCommon
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(SmartDashboard_DirectCommon PUBLIC cxx_std_20)

add_library(SmartDashboard_Interface_direct STATIC
    ${DIRECT_PUBLIC_HEADERS}
    src/sd_direct_subscriber_stub.cpp
)

target_include_directories(SmartDashboard_Interface_direct
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(SmartDashboard_Interface_direct PUBLIC cxx_std_20)

target_link_libraries(SmartDashboard_Interface_direct
    PUBLIC
        SmartDashboard_DirectCommon
)
