cmake_minimum_required(VERSION 3.25)
project(marvel_ultimate_alliance VERSION 0.1.0 LANGUAGES C CXX)
set(MUA_VERSION_SUFFIX "-dev")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "Only Windows is supported for now")
endif()

get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _multi_config AND NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo|Release)$")
    message(FATAL_ERROR "Set CMAKE_BUILD_TYPE to Debug, RelWithDebInfo or Release (the presets and tools/build.ps1 do): the ReXGlue runtime and GPU plugin are checked per configuration")
endif()

set(MUA_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/win-amd64")
list(APPEND CMAKE_PREFIX_PATH "${MUA_SDK_DIR}")

# Release binaries must not carry the build machine's folders (__FILE__ in log calls).
add_compile_options("$<$<CONFIG:Release>:-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.>")

include(cmake/rexglue.cmake)

# TRG Launcher: the third_party/trg-launcher submodule, or a local checkout
# named by MUA_TRG_LAUNCHER_DIR while the submodule is not checked out.
set(MUA_TRG_LAUNCHER_DIR "$ENV{MUA_TRG_LAUNCHER_DIR}" CACHE PATH
    "Local TRG Launcher checkout, used when third_party/trg-launcher is empty")

# Upstream TRG is immutable. Every build compiles a patched copy under the
# binary directory; the submodule checkout must stay at the pinned revision.
set(MUA_TRG_LAUNCHER_REVISION "b384a28c712c9320eea8f9d50639dec49981b6c1")
set(MUA_TRG_LAUNCHER_PATCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/patches/trg-launcher")
set(MUA_TRG_LAUNCHER_PATCH_STAMP_VERSION 1)
file(GLOB _trg_patch_names CONFIGURE_DEPENDS RELATIVE "${MUA_TRG_LAUNCHER_PATCH_DIR}"
    "${MUA_TRG_LAUNCHER_PATCH_DIR}/*.patch")
list(SORT _trg_patch_names)
# The glob dependency only tracks added or removed files; list the patches as
# configure dependencies too so editing one triggers a reconfigure.
foreach(_trg_patch_name IN LISTS _trg_patch_names)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${MUA_TRG_LAUNCHER_PATCH_DIR}/${_trg_patch_name}")
endforeach()

find_package(Git QUIET)
if(NOT Git_FOUND)
    message(FATAL_ERROR "TRG Launcher: git is required to apply patches/trg-launcher")
endif()

# Copies the source into output_dir and applies the patches there. The stamp
# covers the source state and every patch, so the copy is rebuilt only when one
# of them changes. GIT_CEILING_DIRECTORIES keeps git apply from discovering the
# parent repository when the generated copy has no .git of its own.
function(mua_prepare_patched_trg_launcher source_dir output_dir require_pinned)
    set(_source_state "")
    if(EXISTS "${source_dir}/.git")
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse HEAD
                        OUTPUT_VARIABLE _head OUTPUT_STRIP_TRAILING_WHITESPACE
                        RESULT_VARIABLE _head_result ERROR_QUIET)
        if(NOT _head_result EQUAL 0)
            message(FATAL_ERROR "TRG Launcher: ${source_dir} is not a git checkout; run `git submodule update --init third_party/trg-launcher`")
        endif()
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" status --porcelain --untracked-files=normal
                        OUTPUT_VARIABLE _status OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" diff --binary HEAD
                        OUTPUT_VARIABLE _diff)
        string(SHA256 _source_state "${_head}\n${_status}\n${_diff}")
        if(require_pinned)
            if(NOT _head STREQUAL MUA_TRG_LAUNCHER_REVISION)
                message(FATAL_ERROR "TRG Launcher: ${source_dir} is at ${_head}, expected the pinned ${MUA_TRG_LAUNCHER_REVISION}. Run `git submodule update --init third_party/trg-launcher`.")
            endif()
            if(NOT _status STREQUAL "")
                message(FATAL_ERROR "TRG Launcher: ${source_dir} has local changes; run `git -C third_party/trg-launcher checkout .` and configure again.")
            endif()
        endif()
    else()
        file(GLOB_RECURSE _source_files LIST_DIRECTORIES false RELATIVE "${source_dir}" "${source_dir}/*")
        list(SORT _source_files)
        foreach(_file IN LISTS _source_files)
            if(_file MATCHES "(^|/)\\.git(/|$)")
                continue()
            endif()
            file(SHA256 "${source_dir}/${_file}" _file_hash)
            string(APPEND _source_state "${_file}\n${_file_hash}\n")
        endforeach()
        string(SHA256 _source_state "${_source_state}")
    endif()

    set(_fingerprint "${MUA_TRG_LAUNCHER_PATCH_STAMP_VERSION}\n${source_dir}\n${_source_state}\n")
    foreach(_patch_name IN LISTS _trg_patch_names)
        file(SHA256 "${MUA_TRG_LAUNCHER_PATCH_DIR}/${_patch_name}" _patch_hash)
        string(APPEND _fingerprint "${_patch_name}\n${_patch_hash}\n")
    endforeach()
    string(SHA256 _fingerprint "${_fingerprint}")

    set(_stamp "${output_dir}/.mua-trg-patch-stamp")
    if(EXISTS "${_stamp}" AND EXISTS "${output_dir}/CMakeLists.txt")
        file(READ "${_stamp}" _stamp_value)
        if(_stamp_value STREQUAL _fingerprint)
            return()
        endif()
    endif()

    message(STATUS "TRG Launcher: preparing patched copy in ${output_dir}")
    file(REMOVE_RECURSE "${output_dir}")
    file(MAKE_DIRECTORY "${output_dir}")
    file(COPY "${source_dir}/" DESTINATION "${output_dir}" PATTERN ".git" EXCLUDE)

    get_filename_component(_ceiling "${output_dir}" DIRECTORY)
    foreach(_patch_name IN LISTS _trg_patch_names)
        set(_patch "${MUA_TRG_LAUNCHER_PATCH_DIR}/${_patch_name}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_ceiling}"
                        "${GIT_EXECUTABLE}" -C "${output_dir}" apply --check "${_patch}"
                        RESULT_VARIABLE _can_apply ERROR_VARIABLE _apply_error)
        if(NOT _can_apply EQUAL 0)
            message(FATAL_ERROR "TRG Launcher: ${_patch} does not apply to ${source_dir} (${_apply_error}); the patch and the source revision are out of sync.")
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_ceiling}"
                        "${GIT_EXECUTABLE}" -C "${output_dir}" apply "${_patch}"
                        RESULT_VARIABLE _apply_result)
        if(NOT _apply_result EQUAL 0)
            message(FATAL_ERROR "TRG Launcher: failed to apply ${_patch}")
        endif()
    endforeach()
    file(WRITE "${_stamp}" "${_fingerprint}")
endfunction()

set(_trg_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/trg-launcher")
if(EXISTS "${_trg_source_dir}/CMakeLists.txt")
    set(_trg_require_pinned TRUE)
else()
    set(_trg_source_dir "${MUA_TRG_LAUNCHER_DIR}")
    set(_trg_require_pinned FALSE)
endif()
if(NOT EXISTS "${_trg_source_dir}/CMakeLists.txt")
    message(FATAL_ERROR "TRG Launcher not found: add the third_party/trg-launcher submodule or set MUA_TRG_LAUNCHER_DIR")
endif()

set(_trg_patched_dir "${CMAKE_BINARY_DIR}/trg-launcher-patched")
mua_prepare_patched_trg_launcher("${_trg_source_dir}" "${_trg_patched_dir}" ${_trg_require_pinned})

# Reconfigure when the patches, the upstream checkout or its HEAD change.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_trg_source_dir}/CMakeLists.txt")
if(EXISTS "${_trg_source_dir}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_trg_source_dir}" rev-parse --git-path HEAD
                    OUTPUT_VARIABLE _trg_head_path OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_trg_head_path)
        get_filename_component(_trg_head_path "${_trg_head_path}" ABSOLUTE BASE_DIR "${_trg_source_dir}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_trg_head_path}")
    endif()
endif()

# The launcher draws inside the runtime's own Dear ImGui frame, so it must use
# the ImGui compiled into rexruntime (one context), never a downloaded copy.
add_library(mua_imgui INTERFACE)
target_link_libraries(mua_imgui INTERFACE rex::runtime)
set(TRG_LAUNCHER_IMGUI_TARGET mua_imgui CACHE STRING "" FORCE)
set(TRG_LAUNCHER_STANDALONE OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_FETCH_DEPS OFF CACHE BOOL "" FORCE)
add_subdirectory("${_trg_patched_dir}" trg-launcher)
# Written for C++17: in C++20 and later u8string() is a different type.
set_target_properties(trg_launcher PROPERTIES CXX_STANDARD 17)
# TRG defines this only for cl.exe; clang++ needs it too for the MSVC CRT.
target_compile_definitions(trg_launcher PRIVATE _CRT_SECURE_NO_WARNINGS)
get_directory_property(MUA_TRG_VERSION DIRECTORY "${_trg_patched_dir}" DEFINITION PROJECT_VERSION)

set(MUA_SOURCES
    src/main.cpp
    src/active_log.cpp
    src/audio_mixer_priority.cpp
    src/audio_pcm_firewall.cpp
    src/crash_report.cpp
    src/crt_native.cpp
    src/diagnostics.cpp
    src/disc_identity.cpp
    src/game_definition.cpp
    src/game_files.cpp
    src/game_install.cpp
    src/game_library.cpp
    src/game_profile.cpp
    src/graphics_adapters.cpp
    src/frame_counter.cpp
    src/frame_limiter.cpp
    src/frame_wait.cpp
    src/frame_pacing.cpp
    src/gpu_fence_wait.cpp
    src/guest_functions.cpp
    src/imgui_gamepad.cpp
    src/indirect_tail_calls.cpp
    src/input_summary.cpp
    src/launcher.cpp
    src/launcher_achievements.cpp
    src/log_filter.cpp
    src/log_path.cpp
    src/platform.cpp
    src/recomp_image.cpp
    src/save_diagnostics.cpp
    src/session_log.cpp
    src/settings.cpp
    src/shader_storage_repair.cpp
    src/sound_stream_ring.cpp
    src/system_info.cpp
    src/user_data.cpp
    src/window_host.cpp
    src/xex_identity.cpp
    src/xaudio_worker_barrier.cpp
    src/hang_watchdog.cpp
)

set(MUA_APP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/marvel_ultimate_alliance.manifest")
add_executable(marvel_ultimate_alliance WIN32 ${MUA_SOURCES})
target_link_options(marvel_ultimate_alliance PRIVATE "LINKER:/MANIFESTINPUT:${MUA_APP_MANIFEST}")
set_property(TARGET marvel_ultimate_alliance APPEND PROPERTY LINK_DEPENDS "${MUA_APP_MANIFEST}")

mua_setup_target(marvel_ultimate_alliance GPU_PLUGINS xenos)

# ThinLTO in Release and RelWithDebInfo, from the Crackdown PC port; the runtime and GPU plugin are prebuilt. Debug compiles
# at -O0, which marks every function optnone, so LTO would optimize nothing there.
function(mua_enable_thinlto target)
    set(_lto "$<CONFIG:Release,RelWithDebInfo>")
    if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
        # lld-link runs the LTO backend at -O2 unless told otherwise; Release compiles at -O3.
        set(_link -flto=thin "$<$<CONFIG:Release>:-Wl,/opt:lldlto=3>"
            "-Wl,/lldltocache:${CMAKE_BINARY_DIR}/lto-cache")
    else()
        return()
    endif()
    target_compile_options(${target} PRIVATE "$<$<AND:${_lto},$<COMPILE_LANGUAGE:CXX>>:-flto=thin>")
    get_target_property(_type ${target} TYPE)
    if(NOT _type STREQUAL "OBJECT_LIBRARY")
        target_link_options(${target} PRIVATE "$<${_lto}:${_link}>")
    endif()
endfunction()
mua_enable_thinlto(marvel_ultimate_alliance)
if(TARGET marvel_ultimate_alliance_recomp)
    mua_enable_thinlto(marvel_ultimate_alliance_recomp)
endif()
target_link_libraries(marvel_ultimate_alliance PRIVATE trg::launcher shcore synchronization dxgi shell32 dbghelp)
# The SDK stages its DLLs only when the program links, so a rebuilt runtime or GPU plugin has to
# make it link again; tools/build.ps1 also checks the staged copies.
foreach(_sdk_dll rexruntime rexruntimed rexruntimerd rexgpu-xenos rexgpu-xenosd rexgpu-xenosrd)
    if(EXISTS "${MUA_SDK_DIR}/bin/${_sdk_dll}.dll")
        set_property(TARGET marvel_ultimate_alliance APPEND PROPERTY LINK_DEPENDS
            "${MUA_SDK_DIR}/bin/${_sdk_dll}.dll")
    endif()
endforeach()

# Every build checks that tools/rexglue holds the runtime and GPU plugin built from this branch's patches, and that
# the copies next to the program are those; tools/build.ps1 rebuilds them when they are not. The Debug files stay stock.
# Windows PowerShell has a fixed path, unlike a Store pwsh; the module path a parent pwsh sets breaks it.
find_program(MUA_WINDOWS_POWERSHELL NAMES powershell REQUIRED)
set(MUA_REXGLUE_CHECK "${CMAKE_COMMAND}" -E env --unset=PSModulePath
    "${MUA_WINDOWS_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-patched-rexruntime.ps1" -Check -Config $<CONFIG>)
add_custom_target(mua_rexglue_check COMMAND ${MUA_REXGLUE_CHECK} VERBATIM)
add_dependencies(marvel_ultimate_alliance mua_rexglue_check)
add_custom_target(mua_rexglue_staging ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE:rex::gpu-xenos>"
        "$<TARGET_FILE_DIR:marvel_ultimate_alliance>"
    COMMAND ${MUA_REXGLUE_CHECK} -Staged "$<TARGET_FILE_DIR:marvel_ultimate_alliance>"
    VERBATIM
)
add_dependencies(mua_rexglue_staging marvel_ultimate_alliance)

target_compile_definitions(marvel_ultimate_alliance PRIVATE
    MUA_PORT_VERSION="${PROJECT_VERSION}${MUA_VERSION_SUFFIX}"
    MUA_TRG_VERSION="${MUA_TRG_VERSION}"
    MUA_BUILD_CONFIG="$<CONFIG>"
    MUA_INPUT_ACTIVITY_CALLBACK=$<NOT:$<CONFIG:Debug>>)

# The runtime reads its cvars from <exe dir>/marvel_ultimate_alliance.toml.
add_custom_command(TARGET marvel_ultimate_alliance POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/marvel_ultimate_alliance.toml"
        "$<TARGET_FILE_DIR:marvel_ultimate_alliance>"
    VERBATIM
)

# Extracts an Xbox 360 disc image: tools/extract-disc.ps1
add_executable(extract_disc tools/extract_disc/main.cpp)
target_link_libraries(extract_disc PRIVATE trg::launcher rex::runtime)
add_custom_command(TARGET extract_disc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:extract_disc> $<TARGET_FILE_DIR:extract_disc>
    COMMAND_EXPAND_LISTS)

# Writes the decompressed guest image for offline analysis: cmake --build <dir> --target dump_image
add_executable(dump_image EXCLUDE_FROM_ALL tools/dump_image/main.cpp)
target_link_libraries(dump_image PRIVATE rex::runtime)
add_custom_command(TARGET dump_image POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:dump_image> $<TARGET_FILE_DIR:dump_image>
    COMMAND_EXPAND_LISTS)

# Headless tests, not part of the default build: cmake --build <dir> --target mua_tests
# (tools/test.ps1 builds and runs them without the ReXGlue runtime where they do not need it).
include(cmake/tests.cmake)
