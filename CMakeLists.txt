cmake_minimum_required(VERSION 3.25)
project(marvel_ultimate_alliance VERSION 0.1.0 LANGUAGES C CXX)
set(MUA_VERSION_SUFFIX "")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    enable_language(RC)
elseif(NOT (UNIX AND NOT APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64"))
    message(FATAL_ERROR "Windows x64 and Linux x86_64 are supported")
endif()

get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _multi_config AND NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo|Release)$")
    message(FATAL_ERROR "Set CMAKE_BUILD_TYPE to Debug, RelWithDebInfo or Release (the presets and tools/build.ps1 do): the ReXGlue runtime and GPU plugin are checked per configuration")
endif()

# Where tools/setup-sdk.ps1 (Windows) unpacks the pinned SDK, or the extracted linux-amd64 SDK (tools/build.sh).
if(WIN32)
    set(MUA_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/win-amd64")
else()
    set(MUA_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/linux-amd64")
endif()
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

set(MUA_VERSION_DEFINITIONS
    MUA_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MUA_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MUA_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    MUA_VERSION_SUFFIX="${MUA_VERSION_SUFFIX}")

set(MUA_HOST_SOURCES
    src/main.cpp
    src/achievements_overlay.cpp
    src/active_log.cpp
    src/crash_report.cpp
    src/diagnostics.cpp
    src/disc_identity.cpp
    src/dlss_support.cpp
    src/game_definition.cpp
    src/game_files.cpp
    src/game_install.cpp
    src/game_library.cpp
    src/game_profile.cpp
    src/graphics_adapters.cpp
    src/font_atlas_hd.cpp
    src/frame_counter.cpp
    src/frame_wait.cpp
    src/game_data.cpp
    src/guest_functions.cpp
    src/hd_text.cpp
    src/hd_text_vfs.cpp
    src/igb_image.cpp
    src/imgui_gamepad.cpp
    src/input_summary.cpp
    src/launcher.cpp
    src/launcher_achievements.cpp
    src/log_filter.cpp
    src/log_path.cpp
    src/modal_dialog.cpp
    src/platform.cpp
    src/port_version.cpp
    src/recomp_image.cpp
    src/session_log.cpp
    src/settings.cpp
    src/shader_storage_repair.cpp
    src/system_info.cpp
    src/update_config.cpp
    src/updater/github_release.cpp
    src/updater/json.cpp
    src/updater/sha256.cpp
    src/updater/third_party/miniz/miniz.c
    src/updater/update_controller.cpp
    src/updater/update_job.cpp
    src/updater/update_paths.cpp
    src/updater/update_platform.cpp
    src/updater/update_ui.cpp
    src/updater/update_zip.cpp
    src/updater/version.cpp
    src/user_data.cpp
    src/window_host.cpp
    src/xex_identity.cpp
    src/xmlb.cpp
    src/zip_overlay.cpp
    src/hang_watchdog.cpp
)
set_source_files_properties(src/updater/third_party/miniz/miniz.c PROPERTIES COMPILE_OPTIONS "-w")

# The hooks bound to one game's guest addresses; only the programs that carry a recompiled module have them.
set(MUA_RUNTIME_SOURCES
    src/alchemy_native.cpp
    src/aspect_render.cpp
    src/audio_mixer_priority.cpp
    src/audio_pcm_firewall.cpp
    src/crt_native.cpp
    src/d3d_state_filter.cpp
    src/frame_limiter.cpp
    src/frame_pacing.cpp
    src/gpu_fence_wait.cpp
    src/guest_atomics.cpp
    src/guest_hook_report.cpp
    src/guest_semaphore.cpp
    src/indirect_tail_calls.cpp
    src/main_menu.cpp
    src/main_menu_mua2.cpp
    src/player_name_hooks.cpp
    src/save_diagnostics.cpp
    src/skip_intro.cpp
    src/sound_stream_ring.cpp
    src/swap_hooks.cpp
    src/xaudio_worker_barrier.cpp
)

set(MUA_APP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/marvel_ultimate_alliance.manifest")

# ThinLTO in Release and RelWithDebInfo, from the Crackdown PC port; the runtime and GPU plugin are prebuilt. Debug compiles
# at -O0, which marks every function optnone, so LTO would optimize nothing there.
function(mua_enable_thinlto target)
    set(_lto "$<CONFIG:Release,RelWithDebInfo>")
    if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
        # lld-link runs the LTO backend at -O2 unless told otherwise; Release compiles at -O3.
        set(_link -flto=thin "$<$<CONFIG:Release>:-Wl,/opt:lldlto=3>"
            "-Wl,/lldltocache:${CMAKE_BINARY_DIR}/lto-cache")
    elseif(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(_link -flto=thin -fuse-ld=lld "-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/lto-cache")
    else()
        return()
    endif()
    target_compile_options(${target} PRIVATE "$<$<AND:${_lto},$<COMPILE_LANGUAGE:CXX>>:-flto=thin>")
    get_target_property(_type ${target} TYPE)
    if(NOT _type STREQUAL "OBJECT_LIBRARY")
        target_link_options(${target} PRIVATE "$<${_lto}:${_link}>")
    endif()
endfunction()

# Every build checks that tools/rexglue holds the runtime and GPU plugin built from this branch's patches, and that
# the copies next to the program are those; tools/build.ps1 and tools/build.sh rebuild them when they are not. The
# Debug files stay stock.
if(WIN32)
    # Windows PowerShell has a fixed path, unlike a Store pwsh; the module path a parent pwsh sets breaks it.
    find_program(MUA_WINDOWS_POWERSHELL NAMES powershell REQUIRED)
    set(MUA_REXGLUE_CHECK "${CMAKE_COMMAND}" -E env --unset=PSModulePath
        "${MUA_WINDOWS_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-patched-rexruntime.ps1" -Check -Config $<CONFIG>)
    set(MUA_REXGLUE_STAGED -Staged)
else()
    find_program(MUA_BASH bash REQUIRED)
    set(MUA_REXGLUE_CHECK "${MUA_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-patched-rexruntime.sh"
        --check "${MUA_SDK_DIR}" --config $<CONFIG>)
    set(MUA_REXGLUE_STAGED --staged)
endif()
add_custom_target(mua_rexglue_check COMMAND ${MUA_REXGLUE_CHECK} VERBATIM)

# One program per game: it carries that game's recompiled module (generated/<key>), its guest hooks and the host code
# every program shares. The launcher is the program without a module: it only starts the game's own program.
set(MUA_HEADER_ART_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher")
set(MUA_HEADER_ART_FILES
    "${MUA_HEADER_ART_DIR}/header-art-mua1.png"
    "${MUA_HEADER_ART_DIR}/header-art-mua1-gold.png"
    "${MUA_HEADER_ART_DIR}/header-art-mua2.png")

function(mua_add_game_executable target module_key icon_key)
    if(WIN32)
        # Resource 1 is the first icon in the executable: Explorer, the taskbar and SDL's window class use it.
        set(_app_rc "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/${target}.rc")
        set_source_files_properties("${_app_rc}" PROPERTIES
            OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/${icon_key}/app-icon.ico")
        add_executable(${target} WIN32 ${MUA_HOST_SOURCES} "${_app_rc}")
        target_link_options(${target} PRIVATE "LINKER:/MANIFESTINPUT:${MUA_APP_MANIFEST}")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${MUA_APP_MANIFEST}")
    else()
        add_executable(${target} ${MUA_HOST_SOURCES})
    endif()
    if(NOT "${module_key}" STREQUAL "")
        target_sources(${target} PRIVATE ${MUA_RUNTIME_SOURCES})
    endif()

    mua_setup_target(${target} "${module_key}" GPU_PLUGINS xenos)

    mua_enable_thinlto(${target})
    if(TARGET ${target}_recomp)
        mua_enable_thinlto(${target}_recomp)
    endif()
    target_link_libraries(${target} PRIVATE trg::launcher)
    # The SDK stages its libraries only when the program links, so a rebuilt runtime or GPU plugin has to
    # make it link again; tools/build.ps1 and tools/build.sh also check the staged copies.
    if(WIN32)
        target_link_libraries(${target} PRIVATE shcore synchronization dxgi shell32 dbghelp)
        foreach(_sdk_dll rexruntime rexruntimed rexruntimerd rexgpu-xenos rexgpu-xenosd rexgpu-xenosrd)
            if(EXISTS "${MUA_SDK_DIR}/bin/${_sdk_dll}.dll")
                set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
                    "${MUA_SDK_DIR}/bin/${_sdk_dll}.dll")
            endif()
        endforeach()
    else()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM
        )
        foreach(_sdk_lib rexruntime rexruntimed rexruntimerd rexgpu-xenos rexgpu-xenosd rexgpu-xenosrd)
            if(EXISTS "${MUA_SDK_DIR}/lib/lib${_sdk_lib}.so")
                set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
                    "${MUA_SDK_DIR}/lib/lib${_sdk_lib}.so")
            endif()
        endforeach()
    endif()
    add_dependencies(${target} mua_rexglue_check)

    target_compile_definitions(${target} PRIVATE
        ${MUA_VERSION_DEFINITIONS}
        MUA_TRG_VERSION="${MUA_TRG_VERSION}"
        MUA_BUILD_CONFIG="$<CONFIG>"
        MUA_INPUT_ACTIVITY_CALLBACK=$<NOT:$<CONFIG:Debug>>)

    # The runtime reads its cvars from <exe dir>/marvel_ultimate_alliance.toml, whichever game the program carries.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config/marvel_ultimate_alliance.toml"
            "$<TARGET_FILE_DIR:${target}>"
        VERBATIM
    )
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/assets/launcher"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MUA_HEADER_ART_FILES}
            "$<TARGET_FILE_DIR:${target}>/assets/launcher"
        VERBATIM
    )
endfunction()

# The helper the launcher copies out of the installation and starts: it must run while the installed files are replaced.
# The AppImage replaces itself instead (src/updater/update_platform.cpp).
# Release packages ship the MSYS2 CLANG64 build of these sources (tools/build-updater.ps1).
if(WIN32)
    add_executable(trg-updater WIN32
        src/updater/helper_main.cpp
        src/updater/install_transaction.cpp
        src/updater/sha256.cpp
        src/updater/update_job.cpp
        src/updater/update_paths.cpp
        src/updater/update_platform.cpp
    )
    target_include_directories(trg-updater PRIVATE src)
    target_compile_definitions(trg-updater PRIVATE MUA_UPDATER_HELPER_BUILD=1)
    target_link_libraries(trg-updater PRIVATE shell32)
    target_link_options(trg-updater PRIVATE -static)
    set_property(TARGET trg-updater PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_target_properties(trg-updater PROPERTIES OUTPUT_NAME "trg-updater")
endif()

# NVIDIA DLSS Super Resolution (Windows): mua_dlss_bridge.dll is the only code built against NVIDIA's DLSS SDK, which
# tools/setup-dlss-sdk.ps1 downloads. The GPU plugin (patches/rexglue-sdk/0160, 0161) and the launcher's support check
# load it from beside the program, and everything runs without it.
set(MUA_DLSS_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/nvidia-dlss/310.9.16")
if(WIN32 AND EXISTS "${MUA_DLSS_SDK_DIR}/include/nvsdk_ngx_helpers.h"
        AND EXISTS "${MUA_DLSS_SDK_DIR}/lib/Windows_x86_64/rel/nvngx_dlss.dll")
    set(MUA_DLSS_BRIDGE ON CACHE INTERNAL "Whether mua_dlss_bridge.dll is built")
    add_library(mua_dlss_bridge SHARED src/dlss_bridge/dlss_bridge.cpp)
    target_include_directories(mua_dlss_bridge PRIVATE "${MUA_DLSS_SDK_DIR}/include")
    target_compile_definitions(mua_dlss_bridge PRIVATE MUA_DLSS_ENGINE_VERSION="${PROJECT_VERSION}")
    # NVIDIA's nvsdk_ngx_s libraries are built for the static CRT, which also keeps the bridge free of runtime DLLs.
    set_property(TARGET mua_dlss_bridge PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_libraries(mua_dlss_bridge PRIVATE
        "${MUA_DLSS_SDK_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_s$<$<CONFIG:Debug>:_dbg>.lib" version advapi32 user32)
    add_custom_command(TARGET mua_dlss_bridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MUA_DLSS_SDK_DIR}/lib/Windows_x86_64/rel/nvngx_dlss.dll"
            "$<TARGET_FILE_DIR:mua_dlss_bridge>"
        VERBATIM
    )
else()
    set(MUA_DLSS_BRIDGE OFF CACHE INTERNAL "Whether mua_dlss_bridge.dll is built")
endif()

# AMD FSR 3.1 (Windows): AMD's signed amd_fidelityfx_dx12.dll, which tools/setup-fidelityfx.ps1 downloads, goes beside the
# programs unmodified; the GPU plugin (patches/rexglue-sdk/0165) loads it from there.
set(MUA_FIDELITYFX_DLL "${CMAKE_CURRENT_SOURCE_DIR}/tools/amd-fidelityfx/1.1.4/PrebuiltSignedDLL/amd_fidelityfx_dx12.dll")
if(WIN32 AND EXISTS "${MUA_FIDELITYFX_DLL}")
    set(MUA_FSR3_RUNTIME ON CACHE INTERNAL "Whether amd_fidelityfx_dx12.dll is staged")
    add_custom_target(mua_fidelityfx_runtime ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MUA_FIDELITYFX_DLL}" "${CMAKE_BINARY_DIR}"
        VERBATIM
    )
else()
    set(MUA_FSR3_RUNTIME OFF CACHE INTERNAL "Whether amd_fidelityfx_dx12.dll is staged")
endif()

mua_add_game_executable(marvel_ultimate_alliance_bundle "" launcher)
mua_add_game_executable(marvel_ultimate_alliance_mua1 mua1 mua1)
mua_add_game_executable(marvel_ultimate_alliance_mua1_gold mua1-gold mua1-gold)
mua_add_game_executable(marvel_ultimate_alliance_mua2 mua2 mua2)

add_custom_target(mua_rexglue_staging ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE:rex::gpu-xenos>"
        "$<TARGET_FILE_DIR:marvel_ultimate_alliance_bundle>"
    COMMAND ${MUA_REXGLUE_CHECK} ${MUA_REXGLUE_STAGED} "$<TARGET_FILE_DIR:marvel_ultimate_alliance_bundle>"
    VERBATIM
)
add_dependencies(mua_rexglue_staging marvel_ultimate_alliance_bundle marvel_ultimate_alliance_mua1
                 marvel_ultimate_alliance_mua1_gold marvel_ultimate_alliance_mua2)

# Extracts an Xbox 360 disc image: tools/extract-disc.ps1, a Windows development step.
if(WIN32)
    add_executable(extract_disc tools/extract_disc/main.cpp)
    target_link_libraries(extract_disc PRIVATE trg::launcher rex::runtime)
    add_custom_command(TARGET extract_disc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:extract_disc> $<TARGET_FILE_DIR:extract_disc>
        COMMAND_EXPAND_LISTS)
endif()

# Builds the HD text overlay from a game folder and writes the rebuilt atlases: cmake --build <dir> --target
# hd_text_preview, then hd_text_preview <game folder> <output folder> [render scale]
set(MUA_HD_TEXT_SOURCES
    src/font_atlas_hd.cpp
    src/hd_text.cpp
    src/igb_image.cpp
    src/xmlb.cpp
    src/zip_overlay.cpp
    src/updater/third_party/miniz/miniz.c
)
add_executable(hd_text_preview EXCLUDE_FROM_ALL tools/hd_text_preview/main.cpp ${MUA_HD_TEXT_SOURCES})
target_include_directories(hd_text_preview PRIVATE src)

# Writes the decompressed guest image for offline analysis: cmake --build <dir> --target dump_image
add_executable(dump_image EXCLUDE_FROM_ALL tools/dump_image/main.cpp)
target_link_libraries(dump_image PRIVATE rex::runtime)
if(WIN32)
    add_custom_command(TARGET dump_image POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:dump_image> $<TARGET_FILE_DIR:dump_image>
        COMMAND_EXPAND_LISTS)
else()
    set_target_properties(dump_image PROPERTIES BUILD_RPATH "$ORIGIN")
    add_custom_command(TARGET dump_image POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:dump_image>"
        VERBATIM)
endif()

# Headless tests, not part of the default build: cmake --build <dir> --target mua_tests
# (tools/test.ps1 builds and runs them without the ReXGlue runtime where they do not need it).
include(cmake/tests.cmake)
