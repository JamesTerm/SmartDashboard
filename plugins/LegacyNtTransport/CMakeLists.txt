project(LegacyNtTransportPlugin LANGUAGES CXX)

add_library(LegacyNtTransportPlugin SHARED
    src/legacy_nt_transport_plugin.cpp
)

target_include_directories(LegacyNtTransportPlugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/SmartDashboard/src
)

target_compile_definitions(LegacyNtTransportPlugin
    PRIVATE
        SD_TRANSPORT_PLUGIN_EXPORTS=1
        WIN32_LEAN_AND_MEAN=1
        NOMINMAX=1
)

target_compile_features(LegacyNtTransportPlugin PRIVATE cxx_std_20)

target_link_libraries(LegacyNtTransportPlugin
    PRIVATE
        ws2_32
)

set_target_properties(LegacyNtTransportPlugin PROPERTIES
    OUTPUT_NAME "SmartDashboardTransport_LegacyNt"
    FOLDER "plugins/LegacyNt"
)

add_custom_command(TARGET LegacyNtTransportPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SmartDashboardApp>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:LegacyNtTransportPlugin>"
            "$<TARGET_FILE_DIR:SmartDashboardApp>/$<TARGET_FILE_NAME:LegacyNtTransportPlugin>"
    COMMENT "Copying legacy NT plugin next to SmartDashboardApp"
)
