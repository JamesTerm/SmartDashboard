project(NativeLinkTransport LANGUAGES CXX)

add_library(NativeLinkTransportCore STATIC
    src/native_link_core.cpp
    src/native_link_ipc_client.cpp
    src/native_link_ipc_test_server.cpp
)

target_include_directories(NativeLinkTransportCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(NativeLinkTransportCore PUBLIC cxx_std_20)

add_library(NativeLinkTransportPlugin SHARED
    src/native_link_transport_plugin.cpp
)

target_include_directories(NativeLinkTransportPlugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/SmartDashboard/src
)

target_compile_definitions(NativeLinkTransportPlugin
    PRIVATE
        SD_TRANSPORT_PLUGIN_EXPORTS=1
)

target_compile_features(NativeLinkTransportPlugin PRIVATE cxx_std_20)

target_link_libraries(NativeLinkTransportPlugin
    PRIVATE
        NativeLinkTransportCore
)

set_target_properties(NativeLinkTransportPlugin PROPERTIES
    OUTPUT_NAME "SmartDashboardTransport_NativeLink"
)

add_custom_command(TARGET NativeLinkTransportPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SmartDashboardApp>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:NativeLinkTransportPlugin>"
            "$<TARGET_FILE_DIR:SmartDashboardApp>/$<TARGET_FILE_NAME:NativeLinkTransportPlugin>"
    COMMENT "Copying Native Link plugin next to SmartDashboardApp"
)

if(BUILD_TESTING)
    include(FetchContent)

    if(NOT TARGET GTest::gtest)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        )

        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(NativeLinkTransport_tests
        tests/native_link_core_tests.cpp
        tests/native_link_plugin_tests.cpp
        tests/native_link_ipc_client_tests.cpp
        src/native_link_transport_plugin.cpp
    )

    target_include_directories(NativeLinkTransport_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/SmartDashboard/src
    )

    target_link_libraries(NativeLinkTransport_tests
        PRIVATE
            NativeLinkTransportCore
            GTest::gtest_main
    )

    target_compile_features(NativeLinkTransport_tests PRIVATE cxx_std_20)

    include(GoogleTest)
    gtest_discover_tests(NativeLinkTransport_tests)
endif()
