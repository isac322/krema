# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Package managers install files with their build-time mtimes, so after a
# system upgrade the new headers look older than existing object files and
# mtime-based incremental builds keep objects compiled against the old
# headers. A ctime/inode fingerprint of the found files, plus directory ctimes
# covering nested dependencies, touches a forced-include header when they change,
# recompiling every object once; the header's content never changes, so ccache
# keeps that rebuild cheap.
set(_KREMA_SYSTEM_DEPS_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(krema_system_deps_begin)
    set(_stamp "${CMAKE_BINARY_DIR}/krema_system_deps_stamp.h")

    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:-include ${_stamp}>")
endfunction()

function(_krema_system_deps_path_is_inside path root output_variable)
    string(REGEX REPLACE "/+$" "" _root "${root}")
    if("${path}" STREQUAL "${_root}")
        set(${output_variable} TRUE PARENT_SCOPE)
        return()
    endif()

    string(LENGTH "${_root}" _root_length)
    string(LENGTH "${path}" _path_length)
    if(_path_length GREATER _root_length)
        string(SUBSTRING "${path}" 0 ${_root_length} _path_prefix)
        string(SUBSTRING "${path}" ${_root_length} 1 _path_separator)
        if(_path_prefix STREQUAL "${_root}" AND _path_separator STREQUAL "/")
            set(${output_variable} TRUE PARENT_SCOPE)
            return()
        endif()
    endif()

    set(${output_variable} FALSE PARENT_SCOPE)
endfunction()

function(_krema_system_deps_collect_targets directory output_variable)
    set(_collected_targets "${${output_variable}}")

    get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        if(TARGET "${_target}")
            get_target_property(_type "${_target}" TYPE)
            if(_type STREQUAL "EXECUTABLE"
                OR _type STREQUAL "STATIC_LIBRARY"
                OR _type STREQUAL "SHARED_LIBRARY"
                OR _type STREQUAL "MODULE_LIBRARY"
                OR _type STREQUAL "OBJECT_LIBRARY")
                list(APPEND _collected_targets "${_target}")
            endif()
        endif()
    endforeach()

    get_property(_subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(_subdirectory IN LISTS _subdirectories)
        _krema_system_deps_collect_targets("${_subdirectory}" _collected_targets)
    endforeach()

    set(${output_variable} "${_collected_targets}" PARENT_SCOPE)
endfunction()

function(krema_system_deps_end)
    set(_inputs_file "${CMAKE_BINARY_DIR}/krema_system_deps_inputs.txt")
    set(_fingerprint_file "${CMAKE_BINARY_DIR}/krema_system_deps.fingerprint")
    set(_stamp "${CMAKE_BINARY_DIR}/krema_system_deps_stamp.h")
    set(_inputs)
    get_cmake_property(_cache_variables CACHE_VARIABLES)

    foreach(_variable IN LISTS _cache_variables)
        if(_variable MATCHES "(_DIR|_LIBRARY|_INCLUDE_DIR|_EXECUTABLE)$")
            get_property(_value CACHE "${_variable}" PROPERTY VALUE)
            if(IS_ABSOLUTE "${_value}" AND EXISTS "${_value}")
                _krema_system_deps_path_is_inside("${_value}" "${CMAKE_SOURCE_DIR}" _inside_source)
                _krema_system_deps_path_is_inside("${_value}" "${CMAKE_BINARY_DIR}" _inside_binary)
                if(NOT _inside_source AND NOT _inside_binary)
                    list(APPEND _inputs "${_value}")
                endif()
            endif()
        endif()
    endforeach()
    set(_check_script "${_KREMA_SYSTEM_DEPS_MODULE_DIR}/KremaSystemDepsCheck.cmake")

    foreach(_compiler CMAKE_C_COMPILER CMAKE_CXX_COMPILER)
        if(IS_ABSOLUTE "${${_compiler}}" AND EXISTS "${${_compiler}}")
            list(APPEND _inputs "${${_compiler}}")
        endif()
    endforeach()

    list(REMOVE_DUPLICATES _inputs)
    list(SORT _inputs)
    if(_inputs)
        string(JOIN "\n" _inputs_content ${_inputs})
        string(APPEND _inputs_content "\n")
    else()
        set(_inputs_content "")
    endif()

    set(_rewrite_inputs TRUE)
    if(EXISTS "${_inputs_file}")
        file(READ "${_inputs_file}" _old_inputs_content)
        if(_old_inputs_content STREQUAL _inputs_content)
            set(_rewrite_inputs FALSE)
        endif()
    endif()
    if(_rewrite_inputs)
        file(WRITE "${_inputs_file}" "${_inputs_content}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUTS=${_inputs_file}"
            "-DFINGERPRINT=${_fingerprint_file}"
            "-DSTAMP=${_stamp}"
            -P "${_check_script}"
    )

    add_custom_target(krema_system_deps ALL
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUTS=${_inputs_file}"
            "-DFINGERPRINT=${_fingerprint_file}"
            "-DSTAMP=${_stamp}"
            -P "${_check_script}"
        BYPRODUCTS "${_stamp}"
        COMMENT ""
        VERBATIM
    )

    set(_compiled_targets)
    _krema_system_deps_collect_targets("${CMAKE_SOURCE_DIR}" _compiled_targets)
    list(REMOVE_DUPLICATES _compiled_targets)
    foreach(_target IN LISTS _compiled_targets)
        if(TARGET "${_target}")
            add_dependencies("${_target}" krema_system_deps)
        endif()
    endforeach()
endfunction()
