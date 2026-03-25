project(NT4Transport LANGUAGES CXX)

# --- Core static library (NT4 client) ---

add_library(NT4TransportCore STATIC
    src/nt4_client.cpp
)

target_include_directories(NT4TransportCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(NT4TransportCore PUBLIC cxx_std_20)

# Ian: The NT4 client uses IXWebSocket for WebSocket connectivity. This is
# pulled from vcpkg (ixwebsocket:x64-windows). The Robot_Simulation side
# needed an overlay port to fix subprotocol echoing on the *server* side.
# The client side uses the stock vcpkg package — the subprotocol header is
# sent via setExtraHeaders() and the server handles selection.
find_package(ixwebsocket CONFIG REQUIRED)

target_link_libraries(NT4TransportCore
    PUBLIC
        ixwebsocket::ixwebsocket
    PRIVATE
        bcrypt
)

# --- Plugin shared library ---

add_library(NT4TransportPlugin SHARED
    src/nt4_transport_plugin.cpp
)

target_include_directories(NT4TransportPlugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/SmartDashboard/src
)

target_compile_definitions(NT4TransportPlugin
    PRIVATE
        SD_TRANSPORT_PLUGIN_EXPORTS=1
)

target_compile_features(NT4TransportPlugin PRIVATE cxx_std_20)

target_link_libraries(NT4TransportPlugin
    PRIVATE
        NT4TransportCore
)

set_target_properties(NT4TransportPlugin PROPERTIES
    OUTPUT_NAME "SmartDashboardTransport_NT4"
    FOLDER "plugins/NT4"
)

set_target_properties(NT4TransportCore PROPERTIES
    FOLDER "plugins/NT4"
)

# Ian: POST_BUILD copy step deploys the plugin DLL next to SmartDashboardApp.
# This follows the same pattern as NativeLinkTransport. The host scans for
# SmartDashboardTransport_*.dll at startup and loads them via QLibrary.
add_custom_command(TARGET NT4TransportPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SmartDashboardApp>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:NT4TransportPlugin>"
            "$<TARGET_FILE_DIR:SmartDashboardApp>/$<TARGET_FILE_NAME:NT4TransportPlugin>"
    COMMENT "Copying NT4 transport plugin next to SmartDashboardApp"
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

    add_executable(NT4Transport_tests
        tests/nt4_plugin_tests.cpp
        src/nt4_transport_plugin.cpp
    )

    target_include_directories(NT4Transport_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/SmartDashboard/src
    )

    target_link_libraries(NT4Transport_tests
        PRIVATE
            NT4TransportCore
            GTest::gtest_main
    )

    target_compile_features(NT4Transport_tests PRIVATE cxx_std_20)

    set_target_properties(NT4Transport_tests PROPERTIES
        FOLDER "plugins/NT4"
    )

    include(GoogleTest)
    gtest_discover_tests(NT4Transport_tests
        DISCOVERY_MODE PRE_TEST
    )
endif()
