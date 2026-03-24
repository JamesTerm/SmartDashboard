project(ShuffleboardTransport LANGUAGES CXX)

# --- Core static library (NT4 client) ---

add_library(ShuffleboardTransportCore STATIC
    src/nt4_client.cpp
)

target_include_directories(ShuffleboardTransportCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(ShuffleboardTransportCore PUBLIC cxx_std_20)

# Ian: The NT4 client uses IXWebSocket for WebSocket connectivity. This is
# pulled from vcpkg (ixwebsocket:x64-windows). The Robot_Simulation side
# needed an overlay port to fix subprotocol echoing on the *server* side.
# The client side uses the stock vcpkg package — the subprotocol header is
# sent via setExtraHeaders() and the server handles selection.
find_package(ixwebsocket CONFIG REQUIRED)

target_link_libraries(ShuffleboardTransportCore
    PUBLIC
        ixwebsocket::ixwebsocket
    PRIVATE
        bcrypt
)

# --- Plugin shared library ---

add_library(ShuffleboardTransportPlugin SHARED
    src/shuffleboard_transport_plugin.cpp
)

target_include_directories(ShuffleboardTransportPlugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/SmartDashboard/src
)

target_compile_definitions(ShuffleboardTransportPlugin
    PRIVATE
        SD_TRANSPORT_PLUGIN_EXPORTS=1
)

target_compile_features(ShuffleboardTransportPlugin PRIVATE cxx_std_20)

target_link_libraries(ShuffleboardTransportPlugin
    PRIVATE
        ShuffleboardTransportCore
)

set_target_properties(ShuffleboardTransportPlugin PROPERTIES
    OUTPUT_NAME "SmartDashboardTransport_Shuffleboard"
    FOLDER "plugins/Shuffleboard"
)

set_target_properties(ShuffleboardTransportCore PROPERTIES
    FOLDER "plugins/Shuffleboard"
)

# Ian: POST_BUILD copy step deploys the plugin DLL next to SmartDashboardApp.
# This follows the same pattern as NativeLinkTransport. The host scans for
# SmartDashboardTransport_*.dll at startup and loads them via QLibrary.
add_custom_command(TARGET ShuffleboardTransportPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SmartDashboardApp>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ShuffleboardTransportPlugin>"
            "$<TARGET_FILE_DIR:SmartDashboardApp>/$<TARGET_FILE_NAME:ShuffleboardTransportPlugin>"
    COMMENT "Copying Shuffleboard transport plugin next to SmartDashboardApp"
)

# --- Tests ---

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

    add_executable(ShuffleboardTransport_tests
        tests/shuffleboard_plugin_tests.cpp
        src/shuffleboard_transport_plugin.cpp
    )

    target_include_directories(ShuffleboardTransport_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/SmartDashboard/src
    )

    target_link_libraries(ShuffleboardTransport_tests
        PRIVATE
            ShuffleboardTransportCore
            GTest::gtest_main
    )

    target_compile_features(ShuffleboardTransport_tests PRIVATE cxx_std_20)

    set_target_properties(ShuffleboardTransport_tests PROPERTIES
        FOLDER "plugins/Shuffleboard"
    )

    include(GoogleTest)
    gtest_discover_tests(ShuffleboardTransport_tests
        DISCOVERY_MODE PRE_TEST
    )
endif()
