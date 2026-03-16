cmake_minimum_required(VERSION 3.24)

project(ClientInterface_direct LANGUAGES CXX)

add_library(ClientInterface_direct STATIC
    src/sd_direct_publisher_stub.cpp
    src/sd_direct_retained_store.cpp
    src/sd_smartdashboard_client.cpp
)

target_include_directories(ClientInterface_direct
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/SmartDashboard_Interface_direct/include
)

target_sources(ClientInterface_direct
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/sd_direct_retained_store.h
)

target_compile_features(ClientInterface_direct PUBLIC cxx_std_20)

target_link_libraries(ClientInterface_direct
    PUBLIC
        SmartDashboard_DirectCommon
        SmartDashboard_Interface_direct
)

add_executable(sd_direct_publisher_sample
    samples/publisher_sample.cpp
)

target_link_libraries(sd_direct_publisher_sample
    PRIVATE
        ClientInterface_direct
)

add_executable(sd_command_roundtrip_sample
    samples/command_roundtrip_sample.cpp
)

target_link_libraries(sd_command_roundtrip_sample
    PRIVATE
        ClientInterface_direct
)

add_executable(SmartDashboardCaptureCli
    tools/smartdashboard_capture_cli.cpp
)

add_executable(DirectStateProbeCli
    tools/direct_state_probe_cli.cpp
)

add_executable(DirectWatchCli
    tools/direct_watch_cli.cpp
)

target_link_libraries(SmartDashboardCaptureCli
    PRIVATE
        SmartDashboard_Interface_direct
)

target_link_libraries(DirectStateProbeCli
    PRIVATE
        ClientInterface_direct
)

target_link_libraries(DirectWatchCli
    PRIVATE
        ClientInterface_direct
)

target_include_directories(SmartDashboardCaptureCli
    PRIVATE
        ${CMAKE_SOURCE_DIR}/SmartDashboard_Interface_direct/include
)

target_include_directories(DirectStateProbeCli
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/SmartDashboard_Interface_direct/include
)

target_include_directories(DirectWatchCli
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/SmartDashboard_Interface_direct/include
)

target_compile_features(SmartDashboardCaptureCli PRIVATE cxx_std_20)
target_compile_features(DirectStateProbeCli PRIVATE cxx_std_20)
target_compile_features(DirectWatchCli PRIVATE cxx_std_20)

if(BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(ClientInterface_direct_tests
        tests/capture_cli_tests.cpp
        tests/direct_publisher_tests.cpp
        tests/smartdashboard_client_tests.cpp
        tests/transport_parity_contract_tests.cpp
    )

    target_link_libraries(ClientInterface_direct_tests
        PRIVATE
            ClientInterface_direct
            SmartDashboard_Interface_direct
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(ClientInterface_direct_tests)
endif()
