# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Krema Contributors
#
# Build-time half of KremaSystemDeps.cmake: fingerprints the system
# dependency paths listed in INPUTS and touches STAMP when they changed.

if(NOT DEFINED INPUTS OR NOT DEFINED FINGERPRINT OR NOT DEFINED STAMP)
    message(FATAL_ERROR "KremaSystemDepsCheck.cmake requires INPUTS, FINGERPRINT, and STAMP")
endif()

set(_stamp_content "/* Krema system dependency rebuild trigger. */\n")
set(_files)
set(_missing_lines)
set(_directory_inputs)

if(EXISTS "${INPUTS}")
    file(STRINGS "${INPUTS}" _input_paths)
else()
    set(_input_paths)
endif()

foreach(_input IN LISTS _input_paths)
    if(IS_DIRECTORY "${_input}")
        list(APPEND _directory_inputs "${_input}")
        file(GLOB _directory_files LIST_DIRECTORIES false
            "${_input}/*"
            "${_input}/.*"
        )
        foreach(_directory_file IN LISTS _directory_files)
            if(NOT IS_DIRECTORY "${_directory_file}")
                list(APPEND _files "${_directory_file}")
            endif()
        endforeach()
    elseif(EXISTS "${_input}")
        list(APPEND _files "${_input}")
    else()
        list(APPEND _missing_lines "${_input}|missing")
    endif()
endforeach()

list(REMOVE_DUPLICATES _files)
list(SORT _files)
list(REMOVE_DUPLICATES _directory_inputs)
if(_directory_inputs)
    execute_process(
        COMMAND find -H ${_directory_inputs}
            -mindepth 1
            -type d
            -printf "%p|%i|%C@\\n"
        RESULT_VARIABLE _find_result
        OUTPUT_VARIABLE _find_output
        ERROR_VARIABLE _find_error
    )
else()
    set(_find_output "")
endif()

if(_files)
    execute_process(
        COMMAND stat -L "--format=%n|%i|%s|%Y|%Z" ${_files}
        RESULT_VARIABLE _stat_result
        OUTPUT_VARIABLE _stat_output
        ERROR_VARIABLE _stat_error
    )
else()
    set(_stat_result 0)
    set(_stat_output "")
    set(_stat_error "")
endif()

string(REPLACE "\r\n" "\n" _stat_output "${_stat_output}")
string(REGEX REPLACE "\n+$" "" _stat_output "${_stat_output}")
if(_stat_output STREQUAL "")
    set(_fingerprint_lines)
else()
    string(REPLACE "\n" ";" _fingerprint_lines "${_stat_output}")
endif()
string(REPLACE "\r\n" "\n" _find_output "${_find_output}")
string(REGEX REPLACE "\n+$" "" _find_output "${_find_output}")
if(NOT _find_output STREQUAL "")
    string(REPLACE "\n" ";" _find_lines "${_find_output}")
    list(APPEND _fingerprint_lines ${_find_lines})
endif()
list(APPEND _fingerprint_lines ${_missing_lines})
list(REMOVE_DUPLICATES _fingerprint_lines)
list(SORT _fingerprint_lines)
if(_fingerprint_lines)
    string(JOIN "\n" _fingerprint_text ${_fingerprint_lines})
    string(APPEND _fingerprint_text "\n")
else()
    set(_fingerprint_text "")
endif()
string(SHA256 _current_fingerprint "${_fingerprint_text}")

set(_old_fingerprint "")
if(EXISTS "${FINGERPRINT}")
    file(READ "${FINGERPRINT}" _old_fingerprint)
    string(STRIP "${_old_fingerprint}" _old_fingerprint)
endif()

if(NOT _stat_result EQUAL 0)
    string(STRIP "${_stat_error}" _stat_error)
    message(WARNING "krema: unable to fingerprint system dependencies with stat: ${_stat_error}")
    if(NOT EXISTS "${STAMP}")
        file(WRITE "${STAMP}" "${_stamp_content}")
    endif()
    file(WRITE "${FINGERPRINT}" "${_current_fingerprint}\n")
    file(TOUCH "${STAMP}")
elseif(NOT EXISTS "${FINGERPRINT}")
    file(WRITE "${FINGERPRINT}" "${_current_fingerprint}\n")
    if(NOT EXISTS "${STAMP}")
        file(WRITE "${STAMP}" "${_stamp_content}")
    endif()
elseif(NOT _old_fingerprint STREQUAL _current_fingerprint)
    file(WRITE "${FINGERPRINT}" "${_current_fingerprint}\n")
    if(NOT EXISTS "${STAMP}")
        file(WRITE "${STAMP}" "${_stamp_content}")
    endif()
    file(TOUCH "${STAMP}")
    message(STATUS "krema: system dependencies changed since the last build; recompiling all objects")
elseif(NOT EXISTS "${STAMP}")
    # `ninja clean` removes the stamp (a byproduct) but keeps the fingerprint;
    # every object force-includes it, so recreate it.
    file(WRITE "${STAMP}" "${_stamp_content}")
endif()
