cmake_minimum_required(VERSION 3.24)

project(SmartDashboard LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(SMARTDASHBOARD_ENABLE_DIRECT_REMEMBERED_CONTROLS "Enable dashboard-owned remembered direct control persistence" OFF)
option(SMARTDASHBOARD_ENABLE_TEMPORARY_TILE_DEFAULTS "Seed temporary widget defaults until live data arrives" ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Test)

qt_add_executable(SmartDashboardApp WIN32
    src/main.cpp
    dist/win/app_icon.rc
    src/resources/resources.qrc
    src/app/main_window.h
    src/app/main_window.cpp
    src/app/debug_log_paths.h
    src/app/debug_log_paths.cpp
    src/app/startup_instance_gate.h
    src/app/startup_instance_gate.cpp
    src/model/variable_store.h
    src/model/variable_store.cpp
    src/layout/layout_serializer.h
    src/layout/layout_serializer.cpp
    src/transport/dashboard_transport.h
    src/transport/dashboard_transport.cpp
    src/transport/dashboard_transport_plugin_api.h
    src/widgets/variable_tile.h
    src/widgets/variable_tile.cpp
    src/widgets/line_plot_widget.h
    src/widgets/line_plot_widget.cpp
    src/widgets/playback_timeline_widget.h
    src/widgets/playback_timeline_widget.cpp
    src/widgets/run_browser_dock.h
    src/widgets/run_browser_dock.cpp
    src/widgets/tile_control_widget.h
    src/widgets/tile_control_widget.cpp
    src/camera/camera_stream_source.h
    src/camera/mjpeg_stream_source.h
    src/camera/mjpeg_stream_source.cpp
    src/camera/camera_publisher_discovery.h
    src/camera/camera_publisher_discovery.cpp
    src/widgets/camera_display_widget.h
    src/widgets/camera_display_widget.cpp
    src/widgets/camera_viewer_dock.h
    src/widgets/camera_viewer_dock.cpp
)

target_include_directories(SmartDashboardApp
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/SmartDashboard_Interface_direct/include
        ${CMAKE_SOURCE_DIR}/ClientInterface_direct/include
)

target_link_libraries(SmartDashboardApp
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        SmartDashboard_Interface_direct
        ClientInterface_direct
        ws2_32
)

target_compile_features(SmartDashboardApp PRIVATE cxx_std_20)
target_compile_definitions(SmartDashboardApp PRIVATE
    SMARTDASHBOARD_ENABLE_DIRECT_REMEMBERED_CONTROLS=$<BOOL:${SMARTDASHBOARD_ENABLE_DIRECT_REMEMBERED_CONTROLS}>
    SMARTDASHBOARD_ENABLE_TEMPORARY_TILE_DEFAULTS=$<BOOL:${SMARTDASHBOARD_ENABLE_TEMPORARY_TILE_DEFAULTS}>
)

set_target_properties(SmartDashboardApp PROPERTIES FOLDER "SmartDashboard")

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

    add_executable(SmartDashboard_tests
        tests/line_plot_widget_tests.cpp
        tests/main_window_persistence_tests.cpp
        tests/playback_timeline_widget_tests.cpp
        tests/run_browser_dock_tests.cpp
        tests/tile_control_widget_tests.cpp
        tests/variable_tile_tests.cpp
        tests/startup_instance_gate_tests.cpp
        src/app/debug_log_paths.h
        src/app/debug_log_paths.cpp
        src/app/main_window.h
        src/app/main_window.cpp
        src/app/startup_instance_gate.h
        src/app/startup_instance_gate.cpp
        src/model/variable_store.h
        src/model/variable_store.cpp
        src/layout/layout_serializer.h
        src/layout/layout_serializer.cpp
        src/transport/dashboard_transport.h
        src/transport/dashboard_transport.cpp
        src/widgets/variable_tile.cpp
        src/widgets/line_plot_widget.cpp
        src/widgets/playback_timeline_widget.cpp
        src/widgets/run_browser_dock.cpp
        src/widgets/tile_control_widget.cpp
        src/camera/camera_stream_source.h
        src/camera/mjpeg_stream_source.cpp
        src/camera/camera_publisher_discovery.cpp
        src/widgets/camera_display_widget.cpp
        src/widgets/camera_viewer_dock.cpp
    )

    target_sources(SmartDashboard_tests PRIVATE tests/dashboard_transport_registry_tests.cpp)

    target_include_directories(SmartDashboard_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/SmartDashboard_Interface_direct/include
            ${CMAKE_SOURCE_DIR}/plugins/NativeLinkTransport/include
    )

    target_link_libraries(SmartDashboard_tests
        PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Widgets
            Qt6::Network
            Qt6::Test
            SmartDashboard_Interface_direct
            ClientInterface_direct
            GTest::gtest_main
            NativeLinkTransportCore
    )

    target_compile_features(SmartDashboard_tests PRIVATE cxx_std_20)
    target_compile_definitions(SmartDashboard_tests PRIVATE
        SMARTDASHBOARD_TESTS=1
        SMARTDASHBOARD_ENABLE_DIRECT_REMEMBERED_CONTROLS=$<BOOL:${SMARTDASHBOARD_ENABLE_DIRECT_REMEMBERED_CONTROLS}>
        SMARTDASHBOARD_ENABLE_TEMPORARY_TILE_DEFAULTS=$<BOOL:${SMARTDASHBOARD_ENABLE_TEMPORARY_TILE_DEFAULTS}>
    )

    set_target_properties(SmartDashboard_tests PROPERTIES FOLDER "SmartDashboard")

    include(GoogleTest)
    gtest_discover_tests(SmartDashboard_tests
        DISCOVERY_MODE PRE_TEST
    )
endif()

if(WIN32)
    set(SMARTDASHBOARD_QT_EXTRA_DLL_DIRS "" CACHE STRING "Optional semicolon-separated directories to search for Qt runtime companion DLLs")
    option(SMARTDASHBOARD_VERBOSE_QT_DEPLOY "Show optional Qt deploy diagnostics" OFF)

    set(_qt_conf "${CMAKE_CURRENT_SOURCE_DIR}/dist/win/qt.conf")
    set(_qt_tools_bin "")
    set(_qt_triplet_root "")
    set(_qt_runtime_plugins_subdirs
        platforms
        styles
        imageformats
    )

    if(TARGET Qt6::qmake)
        get_target_property(_qt_qmake_location Qt6::qmake IMPORTED_LOCATION)
        if(_qt_qmake_location)
            get_filename_component(_qt_tools_bin "${_qt_qmake_location}" DIRECTORY)
        endif()
    endif()

    if(DEFINED Qt6_DIR)
        get_filename_component(_qt_share_dir "${Qt6_DIR}" DIRECTORY)
        get_filename_component(_qt_triplet_dir "${_qt_share_dir}" DIRECTORY)
        set(_qt_triplet_root "${_qt_triplet_dir}")

        if(NOT _qt_tools_bin)
            if(EXISTS "${_qt_triplet_root}/tools/Qt6/bin")
                set(_qt_tools_bin "${_qt_triplet_root}/tools/Qt6/bin")
            elseif(EXISTS "${_qt_triplet_root}/tools/qt6/bin")
                set(_qt_tools_bin "${_qt_triplet_root}/tools/qt6/bin")
            endif()
        endif()
    endif()

    set(_windeployqt_hint_dirs "")
    if(_qt_tools_bin)
        list(APPEND _windeployqt_hint_dirs "${_qt_tools_bin}")
    endif()
    if(_qt_triplet_root)
        list(APPEND _windeployqt_hint_dirs "${_qt_triplet_root}/bin")
    endif()

    set(_is_vcpkg_qt OFF)
    if(DEFINED Qt6_DIR)
        file(TO_CMAKE_PATH "${Qt6_DIR}" _qt6_dir_norm)
        # Ian: Match both classic vcpkg (".../installed/<triplet>/share/Qt6")
        # and manifest-mode (".../vcpkg_installed/<triplet>/share/Qt6").
        if(_qt6_dir_norm MATCHES "installed/[^/]+/share/Qt6$")
            set(_is_vcpkg_qt ON)
        endif()
    endif()

    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt windeployqt6 HINTS ${_windeployqt_hint_dirs})
    if(_is_vcpkg_qt)
        set(WINDEPLOYQT_EXECUTABLE "")
    endif()

    set(_vs_debug_path_entries "")
    set(_vs_debug_plugin_path "")

    if(_qt_tools_bin)
        list(APPEND _vs_debug_path_entries "${_qt_tools_bin}" "${_qt_tools_bin}/debug")
    endif()

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET SmartDashboardApp POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:SmartDashboardApp>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_qt_conf}" "$<TARGET_FILE_DIR:SmartDashboardApp>/qt.conf"
            COMMENT "Deploying Qt runtime for SmartDashboardApp"
        )

        foreach(_plugin_dir IN LISTS _qt_runtime_plugins_subdirs)
            add_custom_command(TARGET SmartDashboardApp POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E remove_directory "$<TARGET_FILE_DIR:SmartDashboardApp>/${_plugin_dir}"
            )
        endforeach()
    else()
        set(_vcpkg_installed "")
        set(_vcpkg_triplet "")

        if(DEFINED Qt6_DIR)
            get_filename_component(_qt6_share_dir "${Qt6_DIR}" DIRECTORY)
            get_filename_component(_qt6_triplet_dir "${_qt6_share_dir}" DIRECTORY)
            get_filename_component(_qt6_installed_dir "${_qt6_triplet_dir}" DIRECTORY)

            if(EXISTS "${_qt6_installed_dir}")
                set(_vcpkg_installed "${_qt6_installed_dir}")
                get_filename_component(_vcpkg_triplet "${_qt6_triplet_dir}" NAME)
            endif()
        endif()

        if(DEFINED VCPKG_INSTALLED_DIR)
            set(_vcpkg_installed "${VCPKG_INSTALLED_DIR}")
        elseif(DEFINED CMAKE_TOOLCHAIN_FILE)
            get_filename_component(_vcpkg_root "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
            get_filename_component(_vcpkg_root "${_vcpkg_root}" DIRECTORY)
            get_filename_component(_vcpkg_root "${_vcpkg_root}" DIRECTORY)
            set(_vcpkg_installed "${_vcpkg_root}/installed")
        endif()

        if(DEFINED VCPKG_TARGET_TRIPLET)
            set(_vcpkg_triplet "${VCPKG_TARGET_TRIPLET}")
        elseif(NOT _vcpkg_triplet)
            set(_vcpkg_triplet "x64-windows")
        endif()

        if(_vcpkg_installed)
            set(_vcpkg_bin "${_vcpkg_installed}/${_vcpkg_triplet}/$<IF:$<CONFIG:Debug>,debug/bin,bin>")
            set(_vcpkg_plugins "${_vcpkg_installed}/${_vcpkg_triplet}/$<IF:$<CONFIG:Debug>,debug/Qt6/plugins,Qt6/plugins>")

            list(APPEND _vs_debug_path_entries "${_vcpkg_bin}")
            set(_vs_debug_plugin_path "${_vcpkg_plugins}")

            add_custom_command(TARGET SmartDashboardApp POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -DSOURCE_BIN="${_vcpkg_bin}" -DSOURCE_PLUGINS="${_vcpkg_plugins}"
                        -DDEST_DIR="$<TARGET_FILE_DIR:SmartDashboardApp>" -DQT_CONF="${_qt_conf}" -DQT_TOOLS_BIN="${_qt_tools_bin}" -DEXTRA_DLL_DIRS="${SMARTDASHBOARD_QT_EXTRA_DLL_DIRS}" -DQT_DEPLOY_VERBOSE=${SMARTDASHBOARD_VERBOSE_QT_DEPLOY}
                        -P "${CMAKE_SOURCE_DIR}/cmake/CopyQtRuntime.cmake"
                COMMENT "Copying Qt runtime from vcpkg for SmartDashboardApp"
            )

            foreach(_plugin_dir IN LISTS _qt_runtime_plugins_subdirs)
                add_custom_command(TARGET SmartDashboardApp POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E remove_directory "$<TARGET_FILE_DIR:SmartDashboardApp>/${_plugin_dir}"
                )
            endforeach()
        else()
            message(WARNING "windeployqt not found and vcpkg root could not be resolved. Qt runtime will not be copied next to SmartDashboardApp.")
        endif()
    endif()

    if(_vs_debug_path_entries)
        list(REMOVE_DUPLICATES _vs_debug_path_entries)
        list(JOIN _vs_debug_path_entries ";" _vs_debug_path)
        set(_vs_debug_env "PATH=${_vs_debug_path};%PATH%")
        if(_vs_debug_plugin_path)
            string(APPEND _vs_debug_env "\nQT_PLUGIN_PATH=${_vs_debug_plugin_path}")
        endif()
        set_property(TARGET SmartDashboardApp PROPERTY VS_DEBUGGER_ENVIRONMENT "${_vs_debug_env}")
    endif()

    # Ian: After a plugin rename or deletion, stale SmartDashboardTransport_*.dll
    # files linger in the output directory because CMake incremental builds never
    # remove old artifacts.  LoadPlugins() scans for that glob pattern, so any
    # leftover DLL shows up in the Connection menu as a ghost transport.
    # This POST_BUILD step compares what's on disk against the set of plugin
    # OUTPUT_NAMEs that are actually configured and removes anything extra.
    # The allowlist must mirror the OUTPUT_NAME values set in each plugin's
    # CMakeLists.txt — update it here whenever a plugin is added or renamed.
    set(_valid_transport_dlls
        "SmartDashboardTransport_NativeLink.dll"
        "SmartDashboardTransport_NT4.dll"
    )
    if(SMARTDASHBOARD_BUILD_PLUGIN_LEGACY_NT)
        list(APPEND _valid_transport_dlls "SmartDashboardTransport_LegacyNt.dll")
    endif()

    add_custom_command(TARGET SmartDashboardApp POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DDEPLOY_DIR="$<TARGET_FILE_DIR:SmartDashboardApp>"
            "-DVALID_TRANSPORT_DLLS=${_valid_transport_dlls}"
            -P "${CMAKE_SOURCE_DIR}/cmake/PruneStaleTransportPlugins.cmake"
        COMMENT "Pruning stale transport plugin DLLs from output directory"
    )
endif()
