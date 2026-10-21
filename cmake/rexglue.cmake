find_package(rexglue 0.10.0 QUIET CONFIG)
if(NOT rexglue_FOUND)
    message(FATAL_ERROR "ReXGlue SDK 0.10.0 not found under tools/rexglue: run tools/setup-sdk.ps1")
endif()
message(STATUS "Found ReXGlue SDK ${REXGLUE_VERSION_STRING} at ${rexglue_DIR}")

set(MUA_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated/default")
set(MUA_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/marvel_ultimate_alliance_manifest.toml")

set(MUA_RECOMP_OPTIONS "")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Guest SEH scopes emit __try/__except, which traps hardware faults only under asynchronous EH.
    list(APPEND MUA_RECOMP_OPTIONS -fasync-exceptions -gline-tables-only)
endif()

set(MUA_HAS_RECOMPILED_CODE OFF)
set(MUA_GENERATED_SOURCES_SHA256 none)
if(EXISTS "${MUA_GENERATED_DIR}/sources.cmake")
    include("${MUA_GENERATED_DIR}/sources.cmake")
    set(MUA_HAS_RECOMPILED_CODE ON)
    set_source_files_properties(${GENERATED_SOURCES} PROPERTIES COMPILE_OPTIONS "${MUA_RECOMP_OPTIONS}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MUA_GENERATED_DIR}/sources.cmake")
    file(SHA256 "${MUA_GENERATED_DIR}/sources.cmake" MUA_GENERATED_SOURCES_SHA256)
endif()
# tools/build.ps1 reconfigures when this no longer matches generated/default/sources.cmake.
set(MUA_GENERATED_SOURCES_SHA256 "${MUA_GENERATED_SOURCES_SHA256}" CACHE INTERNAL "")

# Codegen re-runs only when an input listed in codegen.d, or the rexglue tool, changes. The stamp is the only output:
# codegen leaves unchanged sources untouched, so listing them would keep the command permanently out of date.
# ReXGlue's own up-to-date record leaves out the rexglue tool, so a patched tool needs --ignore-stamp.
add_custom_command(
    OUTPUT "${MUA_GENERATED_DIR}/codegen.build.stamp"
    COMMAND $<TARGET_FILE:rex::rexglue> codegen --ignore-stamp "${MUA_MANIFEST}"
    COMMAND ${CMAKE_COMMAND} -E touch "${MUA_GENERATED_DIR}/codegen.build.stamp"
    DEPENDS $<TARGET_FILE:rex::rexglue>
    DEPFILE "${MUA_GENERATED_DIR}/codegen.d"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating recompiled code for marvel_ultimate_alliance"
    VERBATIM
)
add_custom_target(marvel_ultimate_alliance_codegen DEPENDS "${MUA_GENERATED_DIR}/codegen.build.stamp")

function(mua_setup_target target_name)
    if(MUA_HAS_RECOMPILED_CODE)
        add_library(${target_name}_recomp OBJECT ${GENERATED_SOURCES})
        target_include_directories(${target_name}_recomp PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${MUA_GENERATED_DIR}"
        )
        target_link_libraries(${target_name}_recomp PRIVATE rex::runtime)
        add_dependencies(${target_name}_recomp marvel_ultimate_alliance_codegen)
        rexglue_apply_target_settings(${target_name}_recomp)
        # The options go on the target too: clang rejects a PCH whose EH mode differs from the TU including it.
        target_precompile_headers(${target_name}_recomp PRIVATE "${MUA_GENERATED_DIR}/marvel_ultimate_alliance_pch.h")
        target_compile_options(${target_name}_recomp PRIVATE ${MUA_RECOMP_OPTIONS})
        target_link_libraries(${target_name} PRIVATE ${target_name}_recomp)
        target_include_directories(${target_name} PRIVATE "${MUA_GENERATED_DIR}")
        target_compile_definitions(${target_name} PRIVATE MUA_HAS_RECOMPILED_CODE=1)
    endif()
    add_dependencies(${target_name} marvel_ultimate_alliance_codegen)
    target_include_directories(${target_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(${target_name} PRIVATE rex::runtime)
    rexglue_configure_target(${target_name} ${ARGN})
endfunction()
