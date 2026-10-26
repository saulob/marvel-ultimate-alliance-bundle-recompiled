#!/usr/bin/env bash
# Headless Linux checks: the logic, source and runtime tests, built from the Release preset folder. Nothing opens a
# window, plays audio or needs the game files (the tests that read them skip without them). The same lists as
# tools/test.ps1, without its Windows-only tests (Direct3D 12 copy validation, the Winsock updater server).
#
#   tools/test.sh                   configure and build the tests in out/build/linux-amd64-release, then run them
#   tools/test.sh --build-dir DIR   use an already configured Release build folder
#
# The runtime tests need the patched runtime in tools/rexglue/linux-amd64 (tools/build.sh builds it).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/out/build/linux-amd64-release"
if [ "${1:-}" = --build-dir ]; then
  [ $# -ge 2 ] || { echo "--build-dir needs a folder" >&2; exit 2; }
  BUILD="$(realpath "$2")"
fi

[ "$(uname -s)" = Linux ] || { echo "ERROR: runs on Linux only (tools/test.ps1 is the Windows script)" >&2; exit 1; }

HEADLESS=(achievements_overlay aspect_ratio audio_pcm_firewall crash_report diagnostics dpi frame_limiter frame_pacing
  game_data game_install game_library gpu_perf_patches graphics_adapter guest_atomics guest_map guest_semaphore render_scale keyboard
  launcher_handoff launcher_header_art launcher_quit lifecycle log_path multisampling player_names port_version relaunch
  resolve_order runtime_config save_diagnostics sdk_fix shader_storage_repair skip_intro skip_intro_notice
  sound_stream_ring update user_data window_host window_placement xaudio_barrier xma_packet_continuity
  d3d12_async_submission d3d12_sampler_memo achievement_metadata hd_text main_menu main_menu_mua2 title_shutdown
  upscaling world_update_profile)
RUNTIME=(active_log_tests filesystem_mapping_tests frame_stats_tests guest_interrupt_lock_tests log_filter_tests
  physical_heap_stress settings_tests shutdown_order_tests title_thread_shutdown_tests vmsum_tests)

if [ ! -f "$BUILD/build.ninja" ]; then
  (cd "$ROOT" && cmake --preset linux-amd64-release >/dev/null)
fi
cmake --build "$BUILD" --target mua_headless_tests
groups=(headless)
if bash "$ROOT/tools/build-patched-rexruntime.sh" --check "$ROOT/tools/rexglue/linux-amd64" >/dev/null 2>&1; then
  cmake --build "$BUILD" --target mua_runtime_tests
  groups+=(runtime)
else
  echo "WARNING: skipping the runtime tests: tools/rexglue/linux-amd64 is not the patched runtime (run tools/build.sh)" >&2
fi

run() {  # test executable
  echo "== $1"
  (cd "$BUILD" && "./$1")
}

for name in "${HEADLESS[@]}"; do run "mua_${name}_tests"; done
if [[ " ${groups[*]} " == *" runtime "* ]]; then
  for name in "${RUNTIME[@]}"; do run "mua_$name"; done
fi
echo "All headless checks passed."
