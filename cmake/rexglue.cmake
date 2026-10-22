find_package(rexglue 0.10.0 QUIET CONFIG)
if(NOT rexglue_FOUND)
    message(FATAL_ERROR "ReXGlue SDK 0.10.0 not found under tools/rexglue: run tools/setup-sdk.ps1")
endif()
message(STATUS "Found ReXGlue SDK ${REXGLUE_VERSION_STRING} at ${rexglue_DIR}")

set(MUA_RECOMP_OPTIONS "")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Guest SEH scopes emit __try/__except, which traps hardware faults only under asynchronous EH.
    list(APPEND MUA_RECOMP_OPTIONS -fasync-exceptions -gline-tables-only)
endif()

# Each game is recompiled from its own default.xex into generated/<key>, and each executable carries one such module:
# the generated functions are global symbols at the game's own addresses, so two modules cannot share a program.
# Codegen re-runs only when an input listed in codegen.d, or the rexglue tool, changes. The stamp is the only output:
# codegen leaves unchanged sources untouched, so listing them would keep the command permanently out of date.
# ReXGlue's own up-to-date record leaves out the rexglue tool, so a patched tool needs --ignore-stamp.
function(mua_declare_module key codegen_target)
    string(TOUPPER "${key}" _id)
    string(REPLACE "-" "_" _id "${_id}")
    set(_dir "${CMAKE_CURRENT_SOURCE_DIR}/generated/${key}")
    set(_manifest "${CMAKE_CURRENT_SOURCE_DIR}/${key}.manifest.toml")

    set(_sources "")
    set(_sources_sha256 none)
    if(EXISTS "${_dir}/sources.cmake")
        include("${_dir}/sources.cmake")
        set(_sources ${GENERATED_SOURCES})
        set_source_files_properties(${_sources} PROPERTIES COMPILE_OPTIONS "${MUA_RECOMP_OPTIONS}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_dir}/sources.cmake")
        file(SHA256 "${_dir}/sources.cmake" _sources_sha256)
    endif()
    # tools/build.ps1 reconfigures when this no longer matches generated/<key>/sources.cmake.
    set(MUA_GENERATED_SOURCES_SHA256_${_id} "${_sources_sha256}" CACHE INTERNAL "")
    set(MUA_MODULE_${_id}_DIR "${_dir}" PARENT_SCOPE)
    set(MUA_MODULE_${_id}_SOURCES "${_sources}" PARENT_SCOPE)
    set(MUA_MODULE_${_id}_CODEGEN "${codegen_target}" PARENT_SCOPE)

    add_custom_command(
        OUTPUT "${_dir}/codegen.build.stamp"
        COMMAND $<TARGET_FILE:rex::rexglue> codegen --ignore-stamp "${_manifest}"
        COMMAND ${CMAKE_COMMAND} -E touch "${_dir}/codegen.build.stamp"
        DEPENDS $<TARGET_FILE:rex::rexglue>
        DEPFILE "${_dir}/codegen.d"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating recompiled code for ${key}"
        VERBATIM
    )
    add_custom_target(${codegen_target} DEPENDS "${_dir}/codegen.build.stamp")
endfunction()

mua_declare_module(mua1-gold marvel_ultimate_alliance_mua1_gold_codegen)
mua_declare_module(mua1 marvel_ultimate_alliance_mua1_codegen)
mua_declare_module(mua2 marvel_ultimate_alliance_mua2_codegen)

function(mua_setup_target target_name module_key)
    string(TOUPPER "${module_key}" _id)
    string(REPLACE "-" "_" _id "${_id}")
    set(_dir "${MUA_MODULE_${_id}_DIR}")
    if(NOT "${module_key}" STREQUAL "")
        target_compile_definitions(${target_name} PRIVATE MUA_RUNTIME_${_id}=1)
        add_dependencies(${target_name} ${MUA_MODULE_${_id}_CODEGEN})
    endif()
    if(MUA_MODULE_${_id}_SOURCES)
        add_library(${target_name}_recomp OBJECT ${MUA_MODULE_${_id}_SOURCES})
        target_include_directories(${target_name}_recomp PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${_dir}"
        )
        target_link_libraries(${target_name}_recomp PRIVATE rex::runtime)
        add_dependencies(${target_name}_recomp ${MUA_MODULE_${_id}_CODEGEN})
        rexglue_apply_target_settings(${target_name}_recomp)
        # The options go on the target too: clang rejects a PCH whose EH mode differs from the TU including it.
        target_precompile_headers(${target_name}_recomp PRIVATE "${_dir}/marvel_ultimate_alliance_pch.h")
        target_compile_options(${target_name}_recomp PRIVATE ${MUA_RECOMP_OPTIONS})
        target_link_libraries(${target_name} PRIVATE ${target_name}_recomp)
        target_include_directories(${target_name} PRIVATE "${_dir}")
        target_compile_definitions(${target_name} PRIVATE MUA_HAS_RECOMPILED_CODE=1)
    endif()
    target_compile_definitions(${target_name} PRIVATE MUA_RUNTIME_KEY="${module_key}")
    target_include_directories(${target_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(${target_name} PRIVATE rex::runtime)
    rexglue_configure_target(${target_name} ${ARGN})
endfunction()
