if(NOT DEFINED SOURCE_BIN OR NOT DEFINED SOURCE_PLUGINS OR NOT DEFINED DEST_DIR)
    message(FATAL_ERROR "SOURCE_BIN, SOURCE_PLUGINS, and DEST_DIR must be set")
endif()

if(NOT EXISTS "${SOURCE_BIN}")
    message(FATAL_ERROR "SOURCE_BIN does not exist: ${SOURCE_BIN}")
endif()

file(MAKE_DIRECTORY "${DEST_DIR}")
set(_plugins_dest "${DEST_DIR}/plugins")
file(MAKE_DIRECTORY "${_plugins_dest}")

file(GLOB _dlls "${SOURCE_BIN}/*.dll")
if(_dlls)
    file(COPY ${_dlls} DESTINATION "${DEST_DIR}")
endif()

if(EXISTS "${SOURCE_PLUGINS}")
    file(GLOB _plugin_subdirs RELATIVE "${SOURCE_PLUGINS}" "${SOURCE_PLUGINS}/*")
    foreach(_subdir IN LISTS _plugin_subdirs)
        if(IS_DIRECTORY "${SOURCE_PLUGINS}/${_subdir}")
            file(COPY "${SOURCE_PLUGINS}/${_subdir}" DESTINATION "${_plugins_dest}")
        endif()
    endforeach()
endif()

set(_extra_runtime_dlls
    dxcompiler.dll
    dxil.dll
    D3Dcompiler_47.dll
    opengl32sw.dll
)

set(_extra_search_dirs "")
if(DEFINED QT_TOOLS_BIN AND EXISTS "${QT_TOOLS_BIN}")
    list(APPEND _extra_search_dirs "${QT_TOOLS_BIN}" "${QT_TOOLS_BIN}/debug")
endif()

if(DEFINED EXTRA_DLL_DIRS)
    foreach(_dir IN LISTS EXTRA_DLL_DIRS)
        if(EXISTS "${_dir}")
            list(APPEND _extra_search_dirs "${_dir}")
        endif()
    endforeach()
endif()

get_filename_component(_source_bin_parent "${SOURCE_BIN}" DIRECTORY)
get_filename_component(_source_bin_parent_name "${_source_bin_parent}" NAME)
if(_source_bin_parent_name STREQUAL "debug")
    get_filename_component(_triplet_root "${_source_bin_parent}" DIRECTORY)
else()
    set(_triplet_root "${_source_bin_parent}")
endif()

if(EXISTS "${_triplet_root}/tools/Qt6/bin")
    list(APPEND _extra_search_dirs "${_triplet_root}/tools/Qt6/bin" "${_triplet_root}/tools/Qt6/bin/debug")
endif()
if(EXISTS "${_triplet_root}/tools/qt6/bin")
    list(APPEND _extra_search_dirs "${_triplet_root}/tools/qt6/bin" "${_triplet_root}/tools/qt6/bin/debug")
endif()

if(EXISTS "$ENV{WINDIR}/System32")
    list(APPEND _extra_search_dirs "$ENV{WINDIR}/System32")
endif()

list(REMOVE_DUPLICATES _extra_search_dirs)

foreach(_dll IN LISTS _extra_runtime_dlls)
    set(_copied FALSE)
    foreach(_dir IN LISTS _extra_search_dirs)
        if(EXISTS "${_dir}/${_dll}")
            file(COPY "${_dir}/${_dll}" DESTINATION "${DEST_DIR}")
            set(_copied TRUE)
            break()
        endif()
    endforeach()

    if(NOT _copied)
        message(STATUS "CopyQtRuntime: optional runtime DLL not found: ${_dll}")
    endif()
endforeach()

if(DEFINED QT_CONF AND EXISTS "${QT_CONF}")
    file(COPY "${QT_CONF}" DESTINATION "${DEST_DIR}")
endif()
