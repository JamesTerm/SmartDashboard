cmake_minimum_required(VERSION 3.24)

project(SmartDashboardWorkspace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_VS_GLOBALS
        "${CMAKE_VS_GLOBALS};VcpkgXUseBuiltInApplocalDeps=true"
    )
endif()

include(CTest)

# Ian: Push CMake-generated infrastructure targets (ALL_BUILD, ZERO_CHECK,
# RUN_TESTS, INSTALL, ctest dashboard targets) into a hidden folder so they
# don't clutter the top level of the VS Solution Explorer alongside the real
# project targets.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_FOLDER "_CMake")

option(SMARTDASHBOARD_BUILD_PLUGIN_LEGACY_NT "Build legacy NetworkTables transport plugin" ON)

add_subdirectory(SmartDashboard_Interface_direct)
add_subdirectory(ClientInterface_direct)
add_subdirectory(SmartDashboard)

if(SMARTDASHBOARD_BUILD_PLUGIN_LEGACY_NT)
    add_subdirectory(plugins/LegacyNtTransport)
endif()

add_subdirectory(plugins/NativeLinkTransport)
add_subdirectory(plugins/NT4Transport)

# Ian: These transport plugins deploy themselves with POST_BUILD copy steps.
# Make the app/test targets depend on the plugin targets so a normal
# `SmartDashboardApp` rebuild also rebuilds and redeploys the enabled plugins,
# instead of silently leaving the runtime plugins directory stale.
if(TARGET SmartDashboardApp AND TARGET LegacyNtTransportPlugin)
    add_dependencies(SmartDashboardApp LegacyNtTransportPlugin)
endif()

if(TARGET SmartDashboardApp AND TARGET NativeLinkTransportPlugin)
    add_dependencies(SmartDashboardApp NativeLinkTransportPlugin)
endif()

if(TARGET SmartDashboardApp AND TARGET NT4TransportPlugin)
    add_dependencies(SmartDashboardApp NT4TransportPlugin)
endif()

if(BUILD_TESTING AND TARGET SmartDashboard_tests AND TARGET LegacyNtTransportPlugin)
    add_dependencies(SmartDashboard_tests LegacyNtTransportPlugin)
endif()

if(BUILD_TESTING AND TARGET SmartDashboard_tests AND TARGET NativeLinkTransportPlugin)
    add_dependencies(SmartDashboard_tests NativeLinkTransportPlugin)
endif()

if(BUILD_TESTING AND TARGET SmartDashboard_tests AND TARGET NT4TransportPlugin)
    add_dependencies(SmartDashboard_tests NT4TransportPlugin)
endif()
