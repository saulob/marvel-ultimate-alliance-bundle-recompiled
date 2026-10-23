# Headless tests, not part of the default build. tests/<name>_tests.cpp builds as mua_<name>_tests.
#   cmake --build <dir> --target mua_headless_tests   tests that need no ReXGlue runtime
#   cmake --build <dir> --target mua_runtime_tests    tests that run beside the patched runtime
# tools/test.ps1 builds and runs both.

add_custom_target(mua_headless_tests)
add_custom_target(mua_runtime_tests)

function(mua_add_test name group)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS;DEFINES" ${ARGN})
    set(_target mua_${name})
    add_executable(${_target} EXCLUDE_FROM_ALL tests/${name}.cpp ${T_SOURCES})
    target_include_directories(${_target} PRIVATE src)
    target_include_directories(${_target} SYSTEM PRIVATE "${MUA_SDK_DIR}/include" "${_trg_patched_dir}/include")
    target_compile_definitions(${_target} PRIVATE MUA_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src" ${T_DEFINES})
    if(T_LIBS)
        target_link_libraries(${_target} PRIVATE ${T_LIBS})
    endif()
    if("rex::runtime" IN_LIST T_LIBS)
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:${_target}>"
            VERBATIM)
    endif()
    add_dependencies(mua_${group}_tests ${_target})
endfunction()

set(MUA_TEST_BUILD_DEFINES MUA_PORT_VERSION="${PROJECT_VERSION}${MUA_VERSION_SUFFIX}" MUA_BUILD_CONFIG="$<CONFIG>")

mua_add_test(aspect_ratio_tests headless
    DEFINES MUA_GENERATED_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/generated")
mua_add_test(dpi_tests headless)
mua_add_test(window_placement_tests headless)
mua_add_test(launcher_quit_tests headless)
mua_add_test(multisampling_tests headless)
mua_add_test(frame_limiter_tests headless SOURCES src/frame_wait.cpp)
mua_add_test(guest_map_tests headless
    DEFINES MUA_GENERATED_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/generated")
mua_add_test(gpu_perf_patches_tests headless)
mua_add_test(d3d12_async_submission_tests headless)
mua_add_test(d3d12_sampler_memo_tests headless)
mua_add_test(render_scale_tests headless)
mua_add_test(upscaling_tests headless)
mua_add_test(d3d12_copy_validation_tests headless LIBS d3d12 dxgi)
mua_add_test(frame_pacing_tests headless SOURCES src/frame_wait.cpp)
mua_add_test(keyboard_tests headless)
mua_add_test(relaunch_tests headless)
mua_add_test(lifecycle_tests headless)
mua_add_test(window_host_tests headless)
mua_add_test(log_path_tests headless SOURCES src/log_path.cpp)
mua_add_test(diagnostics_tests headless SOURCES src/diagnostics.cpp)
mua_add_test(graphics_adapter_tests headless SOURCES src/graphics_adapters.cpp LIBS trg::launcher dxgi d3d12)
mua_add_test(crash_report_tests headless SOURCES src/crash_report.cpp LIBS shell32 DEFINES ${MUA_TEST_BUILD_DEFINES})
mua_add_test(user_data_tests headless SOURCES src/user_data.cpp)
mua_add_test(game_library_tests headless
    SOURCES src/game_definition.cpp src/game_files.cpp src/game_library.cpp src/game_profile.cpp src/xex_identity.cpp)
mua_add_test(game_install_tests headless
    SOURCES src/game_definition.cpp src/game_files.cpp src/game_install.cpp src/game_library.cpp src/game_profile.cpp
            src/xex_identity.cpp)
mua_add_test(game_data_tests headless
    SOURCES src/game_data.cpp src/game_definition.cpp src/game_profile.cpp)
mua_add_test(achievement_metadata_tests headless
    DEFINES MUA_GENERATED_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/generated")
mua_add_test(shader_storage_repair_tests headless SOURCES src/shader_storage_repair.cpp)
mua_add_test(resolve_order_tests headless DEFINES MUA_SDK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/out/rexglue-sdk-src")
mua_add_test(audio_pcm_firewall_tests headless)
mua_add_test(xaudio_barrier_tests headless)
mua_add_test(sound_stream_ring_tests headless
    DEFINES MUA_GENERATED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/generated/mua1-gold"
            MUA_GENERATED_DIR_MUA1="${CMAKE_CURRENT_SOURCE_DIR}/generated/mua1"
            MUA_GENERATED_DIR_MUA2="${CMAKE_CURRENT_SOURCE_DIR}/generated/mua2")
mua_add_test(save_diagnostics_tests headless
    DEFINES MUA_GENERATED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/generated/mua1-gold"
            MUA_GENERATED_DIR_MUA1="${CMAKE_CURRENT_SOURCE_DIR}/generated/mua1"
            MUA_GENERATED_DIR_MUA2="${CMAKE_CURRENT_SOURCE_DIR}/generated/mua2")
mua_add_test(sdk_fix_tests headless DEFINES MUA_SDK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/out/rexglue-sdk-src")
mua_add_test(runtime_config_tests headless
    DEFINES MUA_STAGED_CONFIG="$<TARGET_FILE_DIR:marvel_ultimate_alliance>/marvel_ultimate_alliance.toml"
            MUA_GPU_PLUGIN_NAME="$<TARGET_FILE_NAME:rex::gpu-xenos>"
            MUA_RUNTIME_NAME="$<TARGET_FILE_NAME:rex::runtime>")

mua_add_test(active_log_tests runtime SOURCES src/active_log.cpp src/log_filter.cpp LIBS rex::runtime)
mua_add_test(frame_stats_tests runtime SOURCES src/hang_watchdog.cpp LIBS rex::runtime dbghelp)
mua_add_test(log_filter_tests runtime
    SOURCES src/active_log.cpp src/crash_report.cpp src/log_filter.cpp src/session_log.cpp
    LIBS rex::runtime shell32 DEFINES ${MUA_TEST_BUILD_DEFINES})
mua_add_test(physical_heap_stress runtime LIBS rex::runtime)
mua_add_test(vmsum_tests runtime LIBS rex::runtime
    DEFINES MUA_GENERATED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/generated/mua1-gold"
            MUA_PCH_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/generated/mua1-gold/marvel_ultimate_alliance_pch.h")
mua_add_test(shutdown_order_tests runtime LIBS rex::runtime)
mua_add_test(title_thread_shutdown_tests runtime LIBS rex::runtime)
mua_add_test(filesystem_mapping_tests runtime LIBS rex::runtime)
mua_add_test(settings_tests runtime
    SOURCES src/settings.cpp src/crash_report.cpp src/diagnostics.cpp src/game_definition.cpp src/game_files.cpp
            src/game_profile.cpp src/graphics_adapters.cpp src/input_summary.cpp src/log_filter.cpp
            src/active_log.cpp src/platform.cpp src/session_log.cpp src/system_info.cpp src/xex_identity.cpp
    LIBS rex::runtime trg::launcher dxgi d3d12 shell32 shcore synchronization
    DEFINES ${MUA_TEST_BUILD_DEFINES})
