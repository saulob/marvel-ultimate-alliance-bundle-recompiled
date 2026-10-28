// Crackdown's GPU patches in this port: the register ranges without write side effects (0081), the host thread
// affinity (0082) and the async submission shutdown order (0080). The predicates are checked on the SDK's own register
// numbers, the wiring on the patch text, and, when the patched SDK source is in out/rexglue-sdk-src, the claim they
// rest on: which registers CommandProcessor::WriteRegister treats specially.
// Run: mua_gpu_perf_patches_tests

#include <bit>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <rex/graphics/register_file.h>

using namespace rex::graphics;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

// The predicate of patches/rexglue-sdk/0081-gpu-side-effect-free-register-ranges.patch.
bool IsSideEffectFreeRegisterRange(uint32_t first_index, uint32_t last_index) {
  static_assert(XE_GPU_REG_DC_LUT_30_COLOR == XE_GPU_REG_DC_LUT_RW_INDEX + 3);
  auto overlaps = [&](uint32_t first, uint32_t last) { return first_index <= last && last_index >= first; };
  return first_index <= last_index && last_index < RegisterFile::kRegisterCount &&
         !overlaps(XE_GPU_REG_SHADER_CONSTANT_000_X, XE_GPU_REG_SHADER_CONSTANT_LOOP_31) &&
         !overlaps(XE_GPU_REG_SCRATCH_REG0, XE_GPU_REG_SCRATCH_REG7) &&
         !overlaps(XE_GPU_REG_COHER_STATUS_HOST, XE_GPU_REG_COHER_STATUS_HOST) &&
         !overlaps(XE_GPU_REG_DC_LUT_RW_INDEX, XE_GPU_REG_DC_LUT_30_COLOR);
}

// The affinity mask of patches/rexglue-sdk/0082-d3d12-host-threads-avoid-cpu0.windows.patch.
uint64_t HostThreadAffinityMask(uint64_t process_mask, bool enabled) {
  if (!enabled || std::popcount(process_mask) < 8) {
    return 0;
  }
  return process_mask & ~uint64_t(0b11);
}

void TestRegisterRanges() {
  std::printf("register ranges without write side effects\n");
  CHECK(IsSideEffectFreeRegisterRange(0x2000, 0x2100));
  CHECK(IsSideEffectFreeRegisterRange(0x0000, 0x0010));
  CHECK(IsSideEffectFreeRegisterRange(XE_GPU_REG_SHADER_CONSTANT_LOOP_31 + 1, RegisterFile::kRegisterCount - 1));

  // Each register with a side effect, alone and inside a longer run.
  const uint32_t special[] = {XE_GPU_REG_SCRATCH_REG0,       XE_GPU_REG_SCRATCH_REG7,    XE_GPU_REG_COHER_STATUS_HOST,
                              XE_GPU_REG_DC_LUT_RW_INDEX,    XE_GPU_REG_DC_LUT_SEQ_COLOR, XE_GPU_REG_DC_LUT_PWL_DATA,
                              XE_GPU_REG_DC_LUT_30_COLOR,    XE_GPU_REG_SHADER_CONSTANT_000_X,
                              XE_GPU_REG_SHADER_CONSTANT_511_W, XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0,
                              XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5, XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031,
                              XE_GPU_REG_SHADER_CONSTANT_LOOP_31};
  for (const uint32_t index : special) {
    CHECK(!IsSideEffectFreeRegisterRange(index, index));
    CHECK(!IsSideEffectFreeRegisterRange(index - 2, index + 2));
    CHECK(!IsSideEffectFreeRegisterRange(index, index + 3));
  }
  // The gamma ramp ports sit between the first and last of the four.
  CHECK(XE_GPU_REG_DC_LUT_SEQ_COLOR > XE_GPU_REG_DC_LUT_RW_INDEX && XE_GPU_REG_DC_LUT_SEQ_COLOR < XE_GPU_REG_DC_LUT_30_COLOR);
  CHECK(XE_GPU_REG_DC_LUT_PWL_DATA > XE_GPU_REG_DC_LUT_RW_INDEX && XE_GPU_REG_DC_LUT_PWL_DATA < XE_GPU_REG_DC_LUT_30_COLOR);

  // Runs next to the special ones are still plain.
  CHECK(IsSideEffectFreeRegisterRange(XE_GPU_REG_SCRATCH_REG7 + 1, XE_GPU_REG_SCRATCH_REG7 + 4));
  CHECK(IsSideEffectFreeRegisterRange(XE_GPU_REG_DC_LUT_30_COLOR + 1, XE_GPU_REG_DC_LUT_30_COLOR + 5));
  CHECK(IsSideEffectFreeRegisterRange(XE_GPU_REG_COHER_STATUS_HOST + 1, XE_GPU_REG_COHER_STATUS_HOST + 3));

  // Empty, reversed and out-of-file runs are never block copies.
  CHECK(!IsSideEffectFreeRegisterRange(10, 9));
  CHECK(!IsSideEffectFreeRegisterRange(RegisterFile::kRegisterCount - 2, RegisterFile::kRegisterCount));
  CHECK(!IsSideEffectFreeRegisterRange(0, 0xFFFFFFFFu));
}

void TestWriteRegisterClaim() {
  std::printf("WriteRegister only treats the four groups specially\n");
  const std::string source = ReadFile(std::string(MUA_SRC_DIR) + "/../out/rexglue-sdk-src/src/graphics/command_processor.cpp");
  if (source.empty()) {
    std::printf("  skipped: patched SDK source not present\n");
    return;
  }
  const size_t begin = source.find("void CommandProcessor::WriteRegister(uint32_t index, uint32_t value)");
  const size_t end = source.find("\nvoid CommandProcessor::WriteRegistersFromMem", begin);
  CHECK(begin != std::string::npos && end != std::string::npos);
  const std::string body = source.substr(begin, end - begin);

  std::set<std::string> cases;
  for (size_t at = body.find("case XE_GPU_REG_"); at != std::string::npos; at = body.find("case XE_GPU_REG_", at + 1)) {
    const size_t name_start = at + 5;
    const size_t name_end = body.find(':', name_start);
    cases.insert(body.substr(name_start, name_end - name_start));
  }
  const std::set<std::string> expected = {"XE_GPU_REG_COHER_STATUS_HOST", "XE_GPU_REG_DC_LUT_RW_INDEX",
                                          "XE_GPU_REG_DC_LUT_SEQ_COLOR", "XE_GPU_REG_DC_LUT_PWL_DATA",
                                          "XE_GPU_REG_DC_LUT_30_COLOR"};
  CHECK(cases == expected);
  CHECK(Has(body, "index >= XE_GPU_REG_SCRATCH_REG0 && index <= XE_GPU_REG_SCRATCH_REG7"));
  // Anything else the function does is the plain store and the unknown-register log.
  CHECK(!Has(body, "XE_GPU_REG_SHADER_CONSTANT"));
}

void TestHostThreadMask() {
  std::printf("host thread affinity\n");
  CHECK(HostThreadAffinityMask(0xFF, true) == 0xFC);
  CHECK(HostThreadAffinityMask(0xFFFF, true) == 0xFFFC);
  CHECK(HostThreadAffinityMask(0xF0F0, true) == 0xF0F0);
  CHECK(HostThreadAffinityMask(0x7F, true) == 0);
  CHECK(HostThreadAffinityMask(0xF, true) == 0);
  CHECK(HostThreadAffinityMask(0, true) == 0);
  CHECK(HostThreadAffinityMask(0xFFFF, false) == 0);
  CHECK(HostThreadAffinityMask(0xFFFFFFFFFFFFFFFFull, true) == 0xFFFFFFFFFFFFFFFCull);
}

void TestPatchWiring() {
  std::printf("patch wiring\n");
  const std::string dir = std::string(MUA_SRC_DIR) + "/../patches/rexglue-sdk/";
  const std::string registers = ReadFile(dir + "0081-gpu-side-effect-free-register-ranges.patch");
  CHECK(Has(registers, "+bool IsSideEffectFreeRegisterRange(uint32_t first_index, uint32_t last_index) {"));
  CHECK(Has(registers, "+  static_assert(XE_GPU_REG_DC_LUT_30_COLOR == XE_GPU_REG_DC_LUT_RW_INDEX + 3);"));
  CHECK(Has(registers, "!overlaps(XE_GPU_REG_SHADER_CONSTANT_000_X, XE_GPU_REG_SHADER_CONSTANT_LOOP_31)"));
  CHECK(Has(registers, "!overlaps(XE_GPU_REG_SCRATCH_REG0, XE_GPU_REG_SCRATCH_REG7)"));
  CHECK(Has(registers, "!overlaps(XE_GPU_REG_COHER_STATUS_HOST, XE_GPU_REG_COHER_STATUS_HOST)"));
  CHECK(Has(registers, "!overlaps(XE_GPU_REG_DC_LUT_RW_INDEX, XE_GPU_REG_DC_LUT_30_COLOR)"));
  CHECK(Has(registers, "+  return IsSideEffectFreeRegisterRange(first_index, last_index) ||"));
  CHECK(Has(registers, "IsSideEffectFreeRegisterRange(start_index, start_index + num_registers - 1)"));
  CHECK(Has(registers, "Unowa001/crackdown-recomp"));

  const std::string threads = ReadFile(dir + "0082-d3d12-host-threads-avoid-cpu0.windows.patch");
  CHECK(Has(threads, "REXCVAR_DEFINE_BOOL(gpu_host_threads_avoid_cpu0, true"));
  CHECK(Has(threads, "std::popcount(uint64_t(process_mask)) < 8"));
  CHECK(Has(threads, "return uint64_t(process_mask) & ~uint64_t(0b11);"));
  CHECK(Has(threads, "command_processor_thread->set_affinity_mask(host_thread_affinity_mask)"));
  CHECK(Has(threads, "async_submission_thread_->set_affinity_mask(host_thread_affinity_mask)"));
  CHECK(Has(threads, "Unowa001/crackdown-recomp"));
}

void TestCurrentReferencePatches() {
  std::printf("current reference patches\n");
  const std::string dir = std::string(MUA_SRC_DIR) + "/../patches/rexglue-sdk/";

  // Crackdown's occlusion query index reuse replaces the drain of every pending query at the wrap of the index space.
  const std::string occlusion = ReadFile(dir + "0075-d3d12-submissions-and-occlusion.windows.patch");
  CHECK(Has(occlusion, "pending_occlusion_queries_.front().host_index == occlusion_query_cursor_"));
  CHECK(Has(occlusion, "std::memset(sample_counts, 0, sizeof(xenos::xe_gpu_depth_sample_counts));"));
  CHECK(!Has(occlusion, "+    CompleteOcclusionQueries(true);\n     occlusion_query_cursor_ = 0;"));

  const std::string bits = ReadFile(dir + "0085-inline-bit-scan-forward.patch");
  CHECK(Has(bits, "+inline bool bit_scan_forward(uint32_t v, uint32_t* out_first_set_index) {"));
  CHECK(Has(bits, "-bool bit_scan_forward(uint32_t v, uint32_t* out_first_set_index) {"));

  const std::string lto = ReadFile(dir + "0086-gpu-plugin-thinlto.windows.patch");
  CHECK(Has(lto, "-flto=thin"));
  CHECK(Has(lto, "$<$<CONFIG:Release>:"));

  const std::string constants = ReadFile(dir + "0087-d3d12-unchanged-constant-writes.windows.patch");
  CHECK(Has(constants, "REXCVAR_DEFINE_BOOL(gpu_skip_unchanged_constant_writes, true"));
  CHECK(Has(constants, "bool StoreSwappedRegisters(uint32_t* __restrict dest"));
  // 0106 removes the residency invalidation these two patches add.
  CHECK(Has(constants, "// Always: the game may have rewritten the vertex data at the same address."));

  const std::string loops = ReadFile(dir + "0088-d3d12-per-draw-loops.windows.patch");
  CHECK(Has(loops, "CopyUsedFloatConstants"));
  CHECK(Has(loops, "vertex_buffers_in_sync_[word] &= ~mask;"));

  const std::string wake = ReadFile(dir + "0089-guest-fence-wake.patch");
  CHECK(Has(wake, "void WakeGuestWordWaiters(const void* host_address);"));
  CHECK(Has(wake, "WakeByAddressAll"));
  CHECK(Has(wake, "EVENT_WRITE_SHD"));

  const std::string game = ReadFile(std::string(MUA_SRC_DIR) + "/gpu_fence_wait.cpp");
  CHECK(Has(game, "{\"fence\", mua::guest::kFencePollSite, 0, 2},"));
  CHECK(Has(game, "{\"ring_space\", mua::guest::kRingSpacePollSite, 60, 1},"));
  CHECK(Has(game, "{\"ring_progress\", mua::guest::kRingProgressPollSite, 4, 1},"));
  CHECK(Has(game, "++wait.timeouts >= kWaitTimeoutLimit"));
  CHECK(Has(game, "constexpr uint32_t kWaitTimeoutLimit = 8;"));
  CHECK(Has(game, "mua::guest::kDeviceFenceOffset"));
  CHECK(Has(game, "mua::guest::kDeviceFlagsOffset"));
  CHECK(Has(game, "REX_HOOK_RAW(MUA_GUEST_FENCE_BEGIN)"));
  CHECK(Has(game, "REX_HOOK_RAW(MUA_GUEST_FENCE_POLL)"));

  const std::string gold = ReadFile(std::string(MUA_SRC_DIR) + "/guest_map_mua1_gold.h");
  CHECK(Has(gold, "#define MUA_GUEST_FENCE_BEGIN sub_824844C0"));
  CHECK(Has(gold, "#define MUA_GUEST_FENCE_POLL sub_82484670"));
  CHECK(Has(gold, "kRingSpacePollSite = 0x8248A38C;"));
  CHECK(Has(gold, "kRingProgressPollSite = 0x8248A2E8;"));
  CHECK(Has(gold, "kFencePollSite = 0x8248AFB8;"));
  CHECK(Has(gold, "kDeviceFlagsOffset = 10813;"));
  const std::string mua1 = ReadFile(std::string(MUA_SRC_DIR) + "/guest_map_mua1.h");
  CHECK(Has(mua1, "#define MUA_GUEST_FENCE_BEGIN sub_8246C090"));
  CHECK(Has(mua1, "#define MUA_GUEST_FENCE_POLL sub_8246C240"));
  CHECK(Has(mua1, "kRingSpacePollSite = 0x82471C7C;"));
  CHECK(Has(mua1, "kRingProgressPollSite = 0x82471BD8;"));
  CHECK(Has(mua1, "kFencePollSite = 0x824728EC;"));
  CHECK(Has(mua1, "kDeviceFlagsOffset = 10809;"));
  CHECK(Has(mua1, "kDeviceFenceOffset = 10768;") && Has(gold, "kDeviceFenceOffset = 10768;"));
  const std::string mua2 = ReadFile(std::string(MUA_SRC_DIR) + "/guest_map_mua2.h");
  CHECK(Has(mua2, "#define MUA_GUEST_FENCE_BEGIN sub_829BB820"));
  CHECK(Has(mua2, "#define MUA_GUEST_FENCE_POLL sub_829BB918"));
  CHECK(Has(mua2, "kRingSpacePollSite = 0x829C2604;"));
  CHECK(Has(mua2, "kRingProgressPollSite = 0x829C2560;"));
  CHECK(Has(mua2, "kFencePollSite = 0x829C2CD8;"));
  CHECK(Has(mua2, "kDeviceFenceOffset = 10896;"));
  CHECK(Has(mua2, "kDeviceFlagsOffset = 10941;"));
}

void TestSubmissionDiagnostics() {
  std::printf("submission diagnostics\n");
  const std::string dir = std::string(MUA_SRC_DIR) + "/../patches/rexglue-sdk/";
  const std::string diagnostics = ReadFile(dir + "0084-d3d12-submission-diagnostics.windows.patch");
  CHECK(Has(diagnostics, "const HRESULT close_result = command_list_->Close();"));
  CHECK(Has(diagnostics, "the command list failed to close"));
  CHECK(Has(diagnostics, "close_failures.fetch_add"));
  CHECK(Has(diagnostics, "(failure & (failure - 1)) == 0"));
  CHECK(!Has(diagnostics, "+  command_list_->Close();") && !Has(diagnostics, "+    command_list_->Close();"));
  CHECK(Has(diagnostics, "GetDeviceRemovedReason()"));
  CHECK(Has(diagnostics, "ExecuteSubmissionCommands(job.command_allocator, *job.commands, job.submission);"));
  CHECK(Has(diagnostics, "ExecuteSubmissionCommands(command_allocator, deferred_command_list_, submission_current_);"));
  // Nodes whose breadcrumbs never started are counted, not skipped silently.
  CHECK(Has(diagnostics, "command lists tracked, {} not finished"));
  CHECK(Has(diagnostics, "DRED: no GPU page fault"));
  CHECK(Has(diagnostics, "std::string DeferredCommandList::Describe(size_t tail_commands) const {"));
}

void TestPortedReferenceFixes() {
  std::printf("ported reference fixes\n");
  const std::string dir = std::string(MUA_SRC_DIR) + "/../patches/rexglue-sdk/";

  // A rejected call is left out of a new command list; the poisoned one is replaced.
  const std::string rejected = ReadFile(dir + "0093-d3d12-record-without-rejected-commands.windows.patch");
  CHECK(Has(rejected, "RecordWithoutRejectedCommands("));
  CHECK(Has(rejected, "commands.Execute(list.Get(), list_1.Get(), command_limit, &rejected);"));
  CHECK(Has(rejected, "command_list_ = list.Detach();"));
  CHECK(Has(rejected, "kMaxRejectedCommands = 16"));

  const std::string residency = ReadFile(dir + "0106-d3d12-per-draw-vertex-buffer-residency.windows.patch");
  CHECK(Has(residency, "+  uint64_t vertex_buffers_resident[2] = {};"));
  CHECK(Has(residency, "-  std::array<VertexBufferState, 96> vertex_buffer_states_{};"));
  CHECK(Has(residency, "-      if (state.address == vfetch_constant.address && state.size == vfetch_constant.size) {"));

  const std::string fxaa = ReadFile(dir + "0107-d3d12-fxaa-extreme-pipeline-check.windows.patch");
  CHECK(Has(fxaa, "+  if (!fxaa_extreme_pipeline_) {"));

  const std::string occlusion = ReadFile(dir + "0108-d3d12-occlusion-query-end-pairing.windows.patch");
  CHECK(Has(occlusion, "+  if (!active_occlusion_query_.valid) {"));
  CHECK(Has(occlusion, "-      active_occlusion_query_.sample_count_address != sample_count_addr) {"));
  CHECK(Has(occlusion, "+void D3D12CommandProcessor::AbandonActiveOcclusionQuery() {"));
  CHECK(!Has(occlusion, "DEBUG_SAULO"));

  const std::string fp = ReadFile(dir + "0104-host-fp-exception-mask.patch");
  CHECK(Has(fp, "+  thread_state_->context()->fpscr.InitHost();"));
  CHECK(Has(fp, "+  ctx->fpscr.InitHost();"));

  const std::string host_thread_fp = ReadFile(dir + "0139-xhostthread-fpscr-init.patch");
  CHECK(Has(host_thread_fp, "+TEST_CASE(\"XHostThread initializes host floating-point exception masks\""));
  CHECK(Has(host_thread_fp, "+add_executable(xthread_tests"));
  CHECK(!Has(host_thread_fp, "src/system/xthread.cpp"));

  const std::string setcsr = ReadFile(dir + "0140-fpscr-mask-host-exceptions.patch");
  CHECK(Has(setcsr, "-  inline void setcsr(uint32_t csr) noexcept { Platform::setcsr(csr); }"));
  CHECK(Has(setcsr, "+    Platform::InitHostExceptions(csr);\n+    Platform::setcsr(csr);"));
  CHECK(Has(setcsr, "+TEST_CASE(\"fpscr: storeFromGuest on a zeroed csr does not arm traps\""));

  const std::string wake = ReadFile(dir + "0100-command-processor-progress-wake.patch");
  CHECK(Has(wake, "+      WakeGuestWordWaiters(memory_->TranslatePhysical(read_ptr_writeback_ptr_));"));
  CHECK(Has(wake, "+      WakeGuestWordWaiters(memory_->TranslatePhysical(mem_addr));"));

  const std::string latency = ReadFile(dir + "0105-d3d12-present-latency-wait.windows.patch");
  CHECK(Has(latency, "DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT"));
  CHECK(Has(latency, "WaitForSingleObjectEx(paint_context_.frame_latency_waitable, 100, TRUE);"));

  const std::string vblank = ReadFile(dir + "0109-vsync-worker-one-vblank-per-wake.patch");
  CHECK(Has(vblank, "-          while (current_time - last_frame_time >= interval_ticks) {"));
  CHECK(Has(vblank, "+              last_frame_time = current_time;"));

  const std::string tick = ReadFile(dir + "0110-presenter-bounded-ui-tick-wait.windows.patch");
  CHECK(Has(tick, "std::chrono::milliseconds(32)"));

  const std::string pacing = ReadFile(dir + "0102-audio-callback-pacing.patch");
  CHECK(Has(pacing, "REXCVAR_DEFINE_BOOL(audio_callback_pacing, true"));
  CHECK(Has(pacing, "std::chrono::microseconds(5333 * 39 / 40)"));

  const std::string lookup = ReadFile(dir + "0116-object-lookup-already-locked.patch");
  CHECK(Has(lookup, "LookupObject<XObject>(handle, /*already_locked=*/true);"));

  const std::string zero_clamp = ReadFile(dir + "0117-dxbc-vertex-fetch-zero-clamp.patch");
  CHECK(Has(zero_clamp, "a_.OpUShR(size_dest, fetch_constant_src.SelectFromSwizzled(1), dxbc::Src::LU(2));"));
  CHECK(Has(zero_clamp,
            "a_.OpMovC(dxbc::Dest::R(system_temp_result_, needed_words), size_src, result_src, dxbc::Src::LU(0));"));

  // The mechanically matched memcpy/memset addresses (compiled-body inspection, not an existing reference file:
  // no reference repo's own crt_native hook is license-compatible to copy, see crt_native.cpp's header comment).
  const std::string crt = ReadFile(std::string(MUA_SRC_DIR) + "/crt_native.cpp");
  CHECK(Has(crt, "REX_HOOK_RAW(MUA_GUEST_MEMCPY)"));
  CHECK(Has(crt, "REX_HOOK_RAW(MUA_GUEST_MEMSET)"));
  CHECK(Has(crt, "CopyOrderIsForwardSafe"));
  CHECK(Has(crt, "IsFlatHostRange"));

  const std::string mixer = ReadFile(std::string(MUA_SRC_DIR) + "/audio_mixer_priority.cpp");
  CHECK(Has(mixer, "REX_HOOK_RAW(MUA_GUEST_AUDIO_RENDER)"));
  CHECK(Has(mixer, "rex::audio::RaiseAudioThreadPriority()"));

  const std::string pipeline_stats = ReadFile(dir + "0118-d3d12-pipeline-creation-stats.windows.patch");
  CHECK(Has(pipeline_stats, "void ConsumeCreationTimings(uint32_t& finished_count, double& mean_ms, double& worst_ms);"));
  CHECK(Has(pipeline_stats, "pipeline_cache_->ConsumeCreationTimings(finished_count, mean_ms, worst_ms);"));

  const std::string timer_stats = ReadFile(dir + "0119-timer-queue-callback-stats.patch");
  CHECK(Has(timer_stats, "bool stats_enabled_ = std::getenv(\"REX_TIMER_STATS\") != nullptr;"));
  CHECK(Has(timer_stats, "void RecordCallback(const WaitItem& item) {"));

  // Host-RT family (reviewed because Marvel falls back to it on GPUs without rasterizer-ordered-view support):
  // transfer/stencil-region performance from Crackdown's reference tree, both self-gated to Path::kHostRenderTargets.
  const std::string hostrt_transfers = ReadFile(dir + "0120-render-target-transfer-performance.patch");
  CHECK(Has(hostrt_transfers, "uint32_t GetDrawOverwrittenRenderTargets(reg::RB_DEPTHCONTROL normalized_depth_control,"));
  const std::string hostrt_transfers_d3d12 = ReadFile(dir + "0121-d3d12-render-target-transfers.windows.patch");
  CHECK(Has(hostrt_transfers_d3d12, "case Path::kHostRenderTargets: {"));
  CHECK(Has(hostrt_transfers_d3d12, "overwritten = GetDrawOverwrittenRenderTargets("));
  const std::string hostrt_resolve_copy = ReadFile(dir + "0123-d3d12-resolve-copy-into-textures.windows.patch");
  CHECK(Has(hostrt_resolve_copy, "GetPath() == Path::kHostRenderTargets"));
  CHECK(Has(hostrt_resolve_copy, "uint32_t PrepareResolveTextureCopies(const draw_util::ResolveTextureCopy& copy);"));

  const std::string admitted_vfetch = ReadFile(dir + "0124-d3d12-admitted-invalid-vfetch-log.patch");
  CHECK(Has(admitted_vfetch, "allowed_invalid_vfetch_count.fetch_add(1, std::memory_order_relaxed) + 1;"));
  CHECK(Has(admitted_vfetch, "occurrence <= 256 || (occurrence & 255) == 0"));

  const std::string memmove = ReadFile(std::string(MUA_SRC_DIR) + "/crt_native.cpp");
  CHECK(Has(memmove, "REX_HOOK_RAW(MUA_GUEST_MEMMOVE)"));
  CHECK(Has(memmove, "if (ctx.r3.s32 < ctx.r4.s32) {"));
  CHECK(Has(memmove, "REXCVAR_DEFINE_BOOL(mua_native_memmove, true"));
}

void TestPerformancePass() {
  std::printf("performance pass patches\n");
  const std::string dir = std::string(MUA_SRC_DIR) + "/../patches/rexglue-sdk/";

  const std::string index = ReadFile(dir + "0125-texture-cache-resolve-target-index.patch");
  CHECK(Has(index, "+  std::unordered_multimap<uint32_t, Texture*> textures_by_base_page_;"));
  CHECK(Has(index, "+  auto [page_first, page_last] = textures_by_base_page_.equal_range(dest_page);"));
  CHECK(Has(index, "+  textures_by_base_page_.clear();"));
  CHECK(Has(index, "+    textures_by_base_page_.emplace(uint32_t(key.base_page), texture);"));

  const std::string pgo = ReadFile(dir + "0126-gpu-plugin-pgo.windows.patch");
  CHECK(Has(pgo, "set(REXGPU_PGO_USE \"\" CACHE FILEPATH"));
  CHECK(Has(pgo, "$<$<CONFIG:Release>:-fprofile-use=${REXGPU_PGO_USE}>"));
  const std::string tools = ReadFile(std::string(MUA_SRC_DIR) + "/../tools/rexruntime-patch.ps1");
  CHECK(Has(tools, "patches\\rexglue-sdk-pgo\\rexgpu-xenos.profdata"));
  CHECK(Has(tools, "\"build_pgo=$((Get-RuntimePgo).Mode)\""));
  const std::string builder = ReadFile(std::string(MUA_SRC_DIR) + "/../tools/build-patched-rexruntime.ps1");
  CHECK(Has(builder, "\"-DREXGPU_PGO_USE=$pgoProfile\""));

  // FSR 1 and CAS no longer need the FidelityFX SDK; the temporal FSR 2/3 path still does.
  const std::string fsr = ReadFile(dir + "0127-presenter-spatial-upscalers.patch");
  CHECK(Has(fsr, "+    .allowed({\"bilinear\", \"cas\", \"fsr\"})"));
  CHECK(Has(fsr, "-    .allowed({\"bilinear\"})"));
  CHECK(Has(fsr, "+bool IsFsrEffect(GuestOutputPaintConfig::Effect effect) {"));
  CHECK(Has(fsr, "+      kRTVCount = kRTVIndexGuestOutputIntermediate0 + kMaxGuestOutputPaintEffects - 1,"));
  CHECK(Has(fsr, "-  static constexpr size_t kMaxGuestOutputPaintEffects = 1;"));
  CHECK(!Has(fsr, "-      kFsr2,"));

  const std::string summary = ReadFile(dir + "0128-d3d12-gpu-work-summary.windows.patch");
  CHECK(Has(summary, "+REXCVAR_DEFINE_BOOL(gpu_work_summary, true"));
  CHECK(Has(summary, "+  NoteGpuWorkSwap(submission_current_, texture_cache_->draw_resolution_scale_x(),"));
  CHECK(Has(summary, "+  ++g_gpu_work_summary.draws;"));
  CHECK(Has(summary, "+  ++g_gpu_work_summary.copies;"));
  CHECK(Has(summary, "+  g_gpu_work_summary.submission_thread_wait += std::chrono::steady_clock::now() - wait_start;"));
  CHECK(Has(summary, "+  if (edram_mode == xenos::EdramMode::kColorDepth || edram_mode == xenos::EdramMode::kDepthOnly) {"));
  CHECK(Has(summary, "+    NoteGpuWorkDraw(regs, edram_mode);"));
  CHECK(Has(summary, "+  NoteGpuWorkResolve(*register_file_);"));
  CHECK(Has(summary, "+  g_gpu_work_summary.swap_texture_width = frontbuffer_width_unscaled;"));
  CHECK(Has(summary, "+  g_gpu_work_summary.presenter_input_width = guest_output_width;"));
  CHECK(Has(summary, "+    REXGPU_INFO(\"GPU targets ({}, scale {}x{}): {}\""));
  CHECK(Has(summary, "+    LogGuestOutputPaintFlow(guest_output_flow, paint_context_.swap_chain_width,"));
  CHECK(Has(summary, "Presenter: input {}x{} | output {}x{} | swap chain {}x{} | effects: {}"));
  CHECK(!Has(summary, "widest color target"));
}

void TestShutdownOrder() {
  std::printf("shutdown order\n");
  const std::string dir = std::string(MUA_SRC_DIR) + "/../patches/rexglue-sdk/";
  const std::string submission = ReadFile(dir + "0080-d3d12-async-submission.windows.patch");
  CHECK(Has(submission, "+  StopAsyncSubmission();"));
  CHECK(Has(submission, "+D3D12CommandProcessor::~D3D12CommandProcessor() {"));
  CHECK(Has(submission, "Stopping, and everything queued has been submitted."));
  CHECK(Has(submission, "rex::thread::Wait(async_submission_thread_.get(), false);"));

  // A WAIT_REG_MEM in an indirect buffer returns false once Shutdown() clears worker_running_;
  // the stock code logs INDIRECT RINGBUFFER for it.
  const std::string waits = ReadFile(dir + "0072-gpu-waits-and-submissions.patch");
  CHECK(Has(waits, "while (worker_running_ && pending_fns_.empty()"));
}

void TestPerformanceSummary() {
  std::printf("performance summary\n");
  const std::string dir = std::string(MUA_SRC_DIR) + "/../patches/rexglue-sdk/";
  const std::string summary = ReadFile(dir + "0130-performance-summary-and-audio-queue-log.windows.patch");
  CHECK(Has(summary, "+  uint64_t idle_wait_ns_ = 0;"));
  CHECK(Has(summary, "+      idle_wait_ns_ += uint64_t("));
  CHECK(Has(summary, "+    g_gpu_work_summary.vertices += primitive_processing_result.host_draw_vertex_count;"));
  CHECK(Has(summary, "+        \"Performance: {:.1f} fps | frame {:.1f} ms | draws {:.0f} | verts {:.0f} | stalls {:.1f} | \""));
  CHECK(Has(summary, "+  if (swaps < 30 || frame_ms <= 0.0) {"));
  CHECK(Has(summary, "+      return \"CPU / COMMAND PROCESSOR LIMITED\";"));
  CHECK(Has(summary, "+  REXAPU_INFO(\"audio queue: audio_maxqframes={} (effective {} frames)\""));

  const std::string config = ReadFile(std::string(MUA_SRC_DIR) + "/../config/marvel_ultimate_alliance.toml");
  CHECK(Has(config, "audio_maxqframes = 16"));

  // The decoder input behind each rejected XMA frame, slow work, lock waits and empty-queue callbacks.
  const std::string audio = ReadFile(dir + "0131-audio-diagnostics.patch");
  CHECK(Has(audio, "+    ReportXmaDecodeError(msg);\n"));
  CHECK(Has(audio, "+  t_trace = &trace;"));
  CHECK(Has(audio, "+  const WorkTimer work_timer(id());\n"));
  CHECK(Has(audio, "+          NoteXmaLockWait(context_id, lock_ms);"));
  CHECK(Has(audio, "+      g_underruns.fetch_add(1, std::memory_order_relaxed);\n"));
  CHECK(Has(audio, "\"audio queue 10s: {} silent frame(s) for an empty queue | max queued {}\""));
  CHECK(Has(audio, "source changed {}"));
  CHECK(Has(audio, "+  if (!NoteErrorKind(message) || g_error_lines.fetch_add(1) >= kMaxErrorLines) {"));
  CHECK(Has(audio, "max work {:.2f} ms | lock waits >5ms {} | repeats {}"));

  const std::string pso = ReadFile(dir + "0133-slow-pipeline-log.windows.patch");
  CHECK(Has(pso, "+      if (elapsed_us >= 10000 && slow_reports.fetch_add(1, std::memory_order_relaxed) < 60) {"));
  CHECK(Has(pso, "\"PSO compile: {:.1f} ms | vs {:016X} ps {:016X} | {}\""));
  for (const std::string* patch : {&audio, &pso}) {
    CHECK(!Has(*patch, "DEBUG_SAULO"));
    CHECK(!Has(*patch, "DebugSaulo"));
  }

  // The counters are common: the audio history (0138) reads them on every platform, the D3D12 line only on Windows.
  const std::string counters = ReadFile(dir + "0132-guest-frame-counters.patch");
  CHECK(Has(counters, "+  kGuestUpdateNs,"));
  CHECK(Has(counters, "+    true,   // kGuestUpdates         (running total)"));
  CHECK(!Has(counters, "d3d12"));
  CHECK(!std::filesystem::exists(dir + "0132-guest-frame-counters.windows.patch"));
  CHECK(Has(ReadFile(dir + "0138-audio-history.patch"), "CounterId::kGuestUpdates"));
  const std::string guest = ReadFile(dir + "0136-d3d12-guest-frame-summary.windows.patch");
  CHECK(Has(guest, "+      return \"GUEST / GAME THREAD LIMITED\";"));
  CHECK(Has(guest, "+                            guest.update_ms >= frame_ms * 0.25;"));
  // A frame the host limiter holds a quarter of, with the host waits low, is capped by the setting, not by hardware.
  CHECK(Has(guest, "+  const bool frame_limiter = host_waits_low && guest.available && guest.limiter_wait_ms >= frame_ms * 0.25;"));
  CHECK(Has(guest, "+      return \"FRAME LIMITER\";"));
  CHECK(Has(guest, "+    summary.window_first_guest = ReadGuestFrameTotals();"));
}

// The streamed set of patches/rexglue-sdk/0134 for one page: faults in two frames in a row stream it, more than 64
// uploads in a frame block it, and every 600th frame end forgets both.
struct StreamedPage {
  bool streamed = false;
  bool blocked = false;
  bool faulted = false;
  bool faulted_previous = false;
  uint32_t uploads = 0;
  uint32_t frames = 0;

  // Whether the upload leaves the page streamed (neither valid nor protected).
  bool Upload() {
    if (!streamed) {
      return false;
    }
    if (uploads >= 64) {
      streamed = false;
      blocked = true;
      return false;
    }
    ++uploads;
    return true;
  }
  void EndFrame() {
    uploads = 0;
    if (++frames >= 600) {
      frames = 0;
      streamed = false;
      blocked = false;
    }
    streamed |= faulted && faulted_previous && !blocked;
    faulted_previous = faulted;
    faulted = false;
  }
};

void TestStreamedPageRule() {
  std::printf("streamed page rule\n");
  StreamedPage page;
  page.faulted = true;
  page.EndFrame();
  CHECK(!page.streamed);
  CHECK(!page.Upload());
  page.EndFrame();
  page.faulted = true;
  page.EndFrame();
  CHECK(!page.streamed);  // not in a row
  page.faulted = true;
  page.EndFrame();
  CHECK(page.streamed);
  for (int i = 0; i < 64; ++i) {
    CHECK(page.Upload());
  }
  CHECK(!page.Upload());
  CHECK(!page.streamed && page.blocked);
  page.faulted = true;
  page.EndFrame();
  page.faulted = true;
  page.EndFrame();
  CHECK(!page.streamed);  // blocked until the reset
  while (page.frames != 599) {
    page.EndFrame();
  }
  page.faulted = true;
  page.EndFrame();
  CHECK(!page.blocked && !page.streamed && page.faulted_previous);
  page.faulted = true;
  page.EndFrame();
  CHECK(page.streamed);
}

size_t Count(const std::string& text, const char* needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

// Shared memory requests whose statement passes allow_streamed.
size_t CountStreamedRequests(const std::string& text) {
  size_t count = 0;
  for (size_t at = text.find("RequestRange"); at != std::string::npos; at = text.find("RequestRange", at + 1)) {
    const std::string statement = text.substr(at, text.find(';', at) - at);
    count += Has(statement, ", true)");
  }
  return count;
}

void TestDrawFastPathPatches() {
  std::printf("draw fast path patches\n");
  const std::string dir = std::string(MUA_SRC_DIR) + "/../patches/rexglue-sdk/";

  const std::string streamed = ReadFile(dir + "0134-gpu-streamed-dynamic-pages.patch") +
                               ReadFile(dir + "0156-d3d12-streamed-dynamic-pages.windows.patch");
  CHECK(Has(streamed, "YesterMester/TheSimpsonsGameRecomp"));
  CHECK(Has(streamed, "+REXCVAR_DEFINE_BOOL(gpu_stream_dynamic_pages, false"));
  // Every game runtime turns it on, the launcher does not, and never over a configured value.
  const std::string settings = ReadFile(std::string(MUA_SRC_DIR) + "/settings.cpp");
  CHECK(Has(settings, "if (game && info->source == rex::cvar::Source::kDefault) {"));
  CHECK(Has(settings, "rex::cvar::SetFlagByName(kKey, \"true\");"));
  CHECK(Count(settings, "SetFlagByName(kKey") == 1);
  const std::string app = ReadFile(std::string(MUA_SRC_DIR) + "/marvel_ultimate_alliance_app.h");
  const size_t multisampling = app.find("mua::ApplyMultisampling();");
  const size_t streaming = app.find("mua::ApplyDynamicPageStreaming(mua::GetRecompiledImage().game);");
  CHECK(multisampling != std::string::npos && streaming != std::string::npos && streaming > multisampling &&
        Count(app.substr(multisampling, streaming - multisampling), "\n") == 1);
  CHECK(Has(streamed, "+REXCVAR_DEFINE_BOOL(gpu_stream_skip_unchanged, true"));
  CHECK(Has(streamed, "+  static constexpr uint32_t kStreamedPagesResetFrames = 600;"));
  CHECK(Has(streamed, "+  static constexpr uint8_t kStreamedPageMaxUploadsPerFrame = 64;"));
  CHECK(Has(streamed, "+  if (!exact_range && streamed_pages_active_) {\n+    MarkPageFaulted(page_first);"));
  CHECK(Has(streamed, "+      system_page_flags_streamed_[block] |= system_page_flags_faulted_[block] &\n"
                      "+                                            system_page_flags_faulted_previous_[block] &\n"
                      "+                                            ~system_page_flags_streamed_blocked_[block];"));
  CHECK(Has(streamed, "+  const bool skip_streamed = upload_allow_streamed_ && !written_by_gpu;"));
  CHECK(Has(streamed, "+    EraseStreamedPageShadows(valid_page_first, valid_page_last);"));
  CHECK(Has(streamed, "+      CopyPagesForUpload(upload_range_start, upload_buffer_pages, upload_buffer_mapping);"));
  CHECK(Has(streamed, "+    shared_memory_->OnGuestFrameEnd();"));
  CHECK(Has(streamed, "+  faulted_blocks_.reserve(num_system_page_flags_);"));
  CHECK(Has(streamed, "RequestRange(vfetch_constant.address << 2, vfetch_request_size, true)"));
  CHECK(Has(streamed, "RequestRange(guest_index_base, guest_index_buffer_needed_bytes, true)"));

  const std::string index = ReadFile(dir + "0135-d3d12-index-buffer-view-memo.windows.patch");
  CHECK(Has(index, "+            index_buffer_view.SizeInBytes = SharedMemory::kBufferSize;"));
  CHECK(Has(index, "+            start_index = guest_index_base >> index_size_log2;"));
  CHECK(Has(index, "+          if (guest_index_base & ((uint32_t(1) << index_size_log2) - 1)) {"));
  CHECK(Has(index, "+        primitive_processing_result.host_draw_vertex_count, 1, start_index, 0, 0);"));
  CHECK(Has(index, "+    current_index_buffer_view_ = {};"));
  // The off-by-default draw fast-path profiler was investigation only.
  CHECK(!std::filesystem::exists(dir + "0136-debug-saulo-draw-fastpath-stats.windows.patch"));

  const std::string sdk = std::string(MUA_SRC_DIR) + "/../out/rexglue-sdk-src/src/graphics/";
  const std::string command_processor = ReadFile(sdk + "d3d12/command_processor.cpp");
  if (command_processor.empty() || !Has(command_processor, "current_index_buffer_view_")) {
    std::printf("  skipped: patched SDK source not present\n");
    return;
  }
  // Streaming is only for data the current draw alone reads.
  const char* const sources[] = {"d3d12/command_processor.cpp", "d3d12/render_target_cache.cpp",
                                 "d3d12/texture_cache.cpp",     "pipeline/texture/cache.cpp",
                                 "primitive_processor.cpp",     "d3d12/primitive_processor.cpp"};
  size_t streamed_requests = 0;
  for (const char* source : sources) {
    streamed_requests += CountStreamedRequests(ReadFile(sdk + source));
  }
  CHECK(streamed_requests == 2);
  CHECK(CountStreamedRequests(command_processor) == 1);
  CHECK(Has(command_processor, "RequestRange(vfetch_constant.address << 2, vfetch_request_size, true)"));
  // IssueDraw is the only index buffer setter, and BeginSubmission forgets the view, itself or through
  // InvalidateCommandListState (0161 shares that reset with DLSS's command list calls).
  CHECK(Count(command_processor, "D3DIASetIndexBuffer(") == 1);
  CHECK(Count(ReadFile(sdk + "d3d12/render_target_cache.cpp"), "IASetIndexBuffer") == 0);
  const size_t begin = command_processor.find("bool D3D12CommandProcessor::BeginSubmission(bool is_guest_command) {");
  const size_t end = command_processor.find("bool D3D12CommandProcessor::EndSubmission(", begin);
  size_t reset = command_processor.find("current_index_buffer_view_ = {};", begin);
  if (reset > end) {
    const size_t invalidate = command_processor.find("void D3D12CommandProcessor::InvalidateCommandListState() {");
    const size_t invalidate_end = command_processor.find("\n}\n", invalidate);
    const bool forgets = invalidate != std::string::npos &&
                         command_processor.find("current_index_buffer_view_ = {};", invalidate) < invalidate_end;
    reset = forgets ? command_processor.find("InvalidateCommandListState();", begin) : std::string::npos;
  }
  CHECK(begin != std::string::npos && reset != std::string::npos && reset < end);
}

// The write fault order of patches/rexglue-sdk/0141-mmio-write-fault-callback-first.patch: callback, then host page
// query, with the stale-page check kept for the race and the old order for reads.
void TestWriteFaultCallbackFirst() {
  std::printf("write fault callback first\n");
  const std::string dir = std::string(MUA_SRC_DIR) + "/../patches/rexglue-sdk/";
  const std::string patch = ReadFile(dir + "0141-mmio-write-fault-callback-first.patch");
  CHECK(Count(patch, "\ndiff --git ") == 2 && Has(patch, "\ndiff --git a/src/system/mmio_handler.cpp b/"));
  CHECK(Has(patch, "\ndiff --git a/src/system/xmemory.cpp b/"));
  CHECK(!Has(patch, "shared_memory") && !Has(patch, "REXCVAR"));

  CHECK(Has(patch, "+    if (is_write && access_violation_callback_ &&\n"));
  CHECK(Has(patch, "+        access_violation_callback_(global_critical_region_.Acquire(),\n"));
  CHECK(Has(patch, "+      return true;\n+    }\n+    // Recheck if the pages are still protected"));
  CHECK(Has(patch, "-    if (access_violation_callback_) {\n"));
  CHECK(Has(patch, "+    if (access_violation_callback_ && !is_write) {\n"));
  CHECK(!Has(patch, "-    memory::QueryProtect(") && !Has(patch, "-    if (cur_access"));

  CHECK(Has(patch, "+    rex::memory::PageAccess host_access;"));
  CHECK(Has(patch,
            "+        host_access != rex::memory::PageAccess::kNoAccess &&\n"
            "+        host_access != rex::memory::PageAccess::kReadOnly) {\n+      return true;"));
  CHECK(!Has(patch, "+    rex::memory::Protect("));

  // When the patched source is staged, the same order in the code itself.
  const std::string sdk = std::string(MUA_SRC_DIR) + "/../out/rexglue-sdk-src/src/system/";
  const std::string mmio = ReadFile(sdk + "mmio_handler.cpp");
  if (Has(mmio, "Writes already went to the callback.")) {
    const size_t body = mmio.find("bool MMIOHandler::ExceptionCallback(");
    const size_t callback = mmio.find("if (is_write && access_violation_callback_ &&", body);
    const size_t query = mmio.find("memory::QueryProtect(fault_host_address", body);
    const size_t read_callback = mmio.find("access_violation_callback_ && !is_write", body);
    CHECK(callback != std::string::npos && callback < query && query < read_callback);
    const std::string xmemory = ReadFile(sdk + "xmemory.cpp");
    const size_t trigger = xmemory.find("physical_heap->TriggerCallbacks(");
    const size_t host_query = xmemory.find("rex::memory::QueryProtect(host_address", trigger);
    const size_t stale_repair = xmemory.find("rex::memory::Protect(reinterpret_cast<void*>(page_base)", trigger);
    CHECK(trigger != std::string::npos && trigger < host_query && host_query < stale_repair);
  }
}

// The wait condition of patches/rexglue-sdk/0143-pipeline-storage-pending-flush.patch.
bool StorageWriterWaits(bool shader, bool write_pipeline, bool flush_shaders, bool flush_pipelines) {
  return !shader && !write_pipeline && !flush_shaders && !flush_pipelines;
}

// The storage writer of 0143 in the D3D12 and Vulkan pipeline caches: a pending flush skips the wait, and the patch
// leaves the storage format, file names, paths and OpenFile alone.
void TestPipelineStoragePendingFlush() {
  std::printf("pipeline storage pending flush\n");
  CHECK(StorageWriterWaits(false, false, false, false));
  CHECK(!StorageWriterWaits(false, false, true, false));
  CHECK(!StorageWriterWaits(false, false, false, true));
  CHECK(!StorageWriterWaits(true, false, false, false));
  CHECK(!StorageWriterWaits(false, true, false, false));

  const std::string dir = std::string(MUA_SRC_DIR) + "/../patches/rexglue-sdk/";
  const std::string patch = ReadFile(dir + "0143-pipeline-storage-pending-flush.patch");
  CHECK(Count(patch, "\ndiff --git ") == 2);
  CHECK(Has(patch, "\ndiff --git a/src/graphics/d3d12/pipeline_cache.cpp b/"));
  CHECK(Has(patch, "\ndiff --git a/src/graphics/vulkan/pipeline_cache.cpp b/"));
  const char* wait = "!shader && !write_pipeline && !flush_shaders && !flush_pipelines) {";
  CHECK(Count(patch, wait) == 2);
  CHECK(Count(patch, "-      if (!shader && !write_pipeline) {\n") == 2);
  CHECK(Count(patch, "\n-") - Count(patch, "\n--- ") == 2);
  const size_t diff_start = patch.find("\ndiff --git ");
  const std::string diff = diff_start == std::string::npos ? std::string() : patch.substr(diff_start);
  CHECK(!Has(diff, "OpenFile") && !Has(diff, "filesystem") && !Has(diff, ".xsh") && !Has(diff, ".xpso") &&
        !Has(diff, "sizeof(") && !Has(diff, "storage_write_request_cond_.notify"));

  // When the patched source is staged, the same condition in the code itself, after the queue and flag checks.
  const std::string sdk = std::string(MUA_SRC_DIR) + "/../out/rexglue-sdk-src/src/graphics/";
  for (const char* cache : {"d3d12/pipeline_cache.cpp", "vulkan/pipeline_cache.cpp"}) {
    const std::string source = ReadFile(sdk + cache);
    if (!Has(source, wait)) {
      continue;
    }
    const size_t pipeline_flag = source.find("flush_pipelines = true;");
    const size_t condition = source.find(wait, pipeline_flag);
    const size_t wake = source.find("storage_write_request_cond_.wait(lock);", condition);
    CHECK(pipeline_flag != std::string::npos && pipeline_flag < condition && condition < wake);
    CHECK(Count(source, "storage_write_request_cond_.wait(lock);") == 1);
    CHECK(Count(source, "if (!shader && !write_pipeline) {") == 0);
  }
}

// NVIDIA Image Scaling (0159) is gone: no patch adds it to the presenter, and no license ships for it. FSR 1 and its
// RCAS pass stay in 0127.
void TestNoNvidiaImageScaling() {
  std::printf("no NVIDIA Image Scaling in the presenter patches\n");
  const std::filesystem::path dir = std::filesystem::path(MUA_SRC_DIR) / ".." / "patches" / "rexglue-sdk";
  int patches = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().extension() != ".patch") {
      continue;
    }
    ++patches;
    const std::string text = ReadFile(entry.path().string());
    CHECK(entry.path().filename().string().rfind("0159-", 0) != 0);
    CHECK(!Has(text, "src/ui/shaders/nis/") && !Has(text, "present_nis_sharpness") && !Has(text, "\"nis\""));
  }
  CHECK(patches > 100);
  const std::string fsr = ReadFile((dir / "0127-presenter-spatial-upscalers.patch").string());
  CHECK(Has(fsr, "+    .allowed({\"bilinear\", \"cas\", \"fsr\"})"));
  const std::string licenses =
      ReadFile(std::string(MUA_SRC_DIR) + "/../packaging/licenses/THIRD-PARTY-LICENSES.txt");
  CHECK(!licenses.empty() && !Has(licenses, "NVIDIA Image Scaling"));
}

// Each patch applies on Windows unless named .linux.patch, and on Linux unless named .windows.patch
// (tools/rexruntime-patch.ps1, tools/build-patched-rexruntime.sh). A Windows-only patch is either Direct3D 12 or Win32
// only by nature, or has its Vulkan half in a .linux patch.
void TestLinuxParityPatches() {
  std::printf("Linux parity: every Direct3D 12 fix and speed-up with a Vulkan meaning has its Vulkan half\n");
  const std::filesystem::path dir = std::filesystem::path(MUA_SRC_DIR) / ".." / "patches" / "rexglue-sdk";
  std::set<std::string> windows_only, linux_only, shared;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    const std::string name = entry.path().filename().string();
    if (entry.path().extension() != ".patch") {
      continue;
    }
    if (name.ends_with(".windows.patch")) {
      windows_only.insert(name.substr(0, 4));
    } else if (name.ends_with(".linux.patch")) {
      linux_only.insert(name.substr(0, 4));
    } else {
      shared.insert(name.substr(0, 4));
    }
  }
  const std::set<std::string> expected_windows_only = {
      "0027", "0049", "0055", "0056", "0075", "0076", "0078", "0080", "0082", "0084", "0086", "0087", "0088", "0090",
      "0091", "0092", "0093", "0103", "0105", "0106", "0107", "0108", "0110", "0114", "0118", "0121", "0123", "0126",
      "0128", "0130", "0133", "0135", "0136", "0156", "0160", "0161", "0162", "0163", "0164", "0165", "0166", "0167",
      "0168", "0169"};
  CHECK(windows_only == expected_windows_only);
  for (const char* now_shared : {"0048", "0085", "0117", "0120", "0122", "0125", "0127", "0134", "0148"}) {
    CHECK(shared.contains(now_shared));
  }
  // The Vulkan halves.
  const std::set<std::string> vulkan_halves = {"0149", "0151", "0170", "0171", "0172", "0173", "0174", "0175",
                                               "0176", "0177", "0178", "0179", "0180", "0181", "0182"};
  for (const std::string& number : vulkan_halves) {
    CHECK(linux_only.contains(number));
  }

  auto patch = [&](const char* name) { return ReadFile((dir / name).string()); };
  const std::string wait = patch("0048-pipeline-wait.patch");
  CHECK(Has(wait, "+REXCVAR_DEFINE_INT32(async_shader_wait_ms, 0, \"GPU\","));
  const std::string vulkan_wait = patch("0170-vulkan-pipeline-wait.linux.patch");
  CHECK(Has(vulkan_wait, "+bool VulkanPipelineCache::WaitForPipeline(void* handle, int64_t& budget_us) const {"));
  CHECK(Has(vulkan_wait, "+    pipeline_wait_left_us_ = int64_t(REXCVAR_GET(async_shader_wait_ms)) * 1000;"));
  CHECK(Has(vulkan_wait, "pipeline_cache_->WaitForPipeline(pipeline_handle, pipeline_wait_left_us_)"));

  const std::string residency = patch("0171-vulkan-per-draw-vertex-buffer-residency.linux.patch");
  CHECK(Has(residency, "+  uint64_t vertex_buffers_resident[2] = {};"));
  CHECK(Has(residency, "-  std::array<VertexBufferState, 96> vertex_buffer_states_{};"));
  CHECK(!Has(residency, "+      VertexBufferState& state"));

  const std::string empty_fetch = patch("0172-vulkan-back-empty-invalid-vertex-fetch.linux.patch");
  CHECK(Has(empty_fetch, "vfetch_constant.type == xenos::FetchConstantType::kInvalidVertex"));
  CHECK(Has(empty_fetch, "+        vfetch_request_size = 4096;"));

  const std::string occlusion = patch("0173-vulkan-occlusion-query-end-pairing.linux.patch");
  CHECK(Has(occlusion, "+void VulkanCommandProcessor::AbandonActiveOcclusionQuery() {"));
  CHECK(Has(occlusion, "+  if (!active_occlusion_query_.valid) {"));
  CHECK(Has(occlusion, "+    std::memset(sample_counts, 0, sizeof(xenos::xe_gpu_depth_sample_counts));"));
  CHECK(Has(occlusion, "-      active_occlusion_query_.sample_count_address != sample_count_addr) {"));

  const std::string zero_clamp = patch("0174-spirv-vertex-fetch-zero-clamp.linux.patch");
  CHECK(Has(zero_clamp, "builder_->makeUintConstant((uint32_t(1) << 24) - 1)"));
  CHECK(Has(zero_clamp, "builder_->createTriOp(spv::OpSelect, words_type, fetch_disabled, words_zero, words);"));

  const std::string wrapper = patch("0175-vulkan-3d-as-2d-wrapper-scaled-size.linux.patch");
  CHECK(Has(wrapper, "+      image_create_info.extent.width *= vulkan_texture_cache.draw_resolution_scale_x();"));
  CHECK(Has(wrapper, "+      image_create_info.extent.height *= vulkan_texture_cache.draw_resolution_scale_y();"));

  const std::string transfers = patch("0176-vulkan-render-target-transfers.linux.patch");
  CHECK(Has(transfers, "+        overwritten = GetDrawOverwrittenRenderTargets("));
  CHECK(Has(transfers, "ext_EXT_shader_stencil_export;"));
  CHECK(Has(transfers, "+          // Entirely inside what the draw replaces."));
  CHECK(Has(transfers, "-      assert_not_zero(transfer_rectangle_count);"));

  const std::string streamed = patch("0177-vulkan-streamed-dynamic-pages.linux.patch");
  CHECK(Has(streamed, "+      CopyPagesForUpload(upload_range_start, uint32_t(upload_buffer_size >> page_size_log2()),"));
  CHECK(Has(streamed, "+    shared_memory_->OnGuestFrameEnd();"));
  CHECK(Has(streamed, "vfetch_request_size, true)) {"));

  const std::string upscalers = patch("0178-vulkan-presenter-spatial-upscalers.linux.patch");
  CHECK(Count(upscalers, "-#if defined(REX_HAS_FIDELITYFX_SDK)") == 7);
  CHECK(!Has(upscalers, "+#if"));
  CHECK(Has(upscalers, "guest_output_ffx_fsr_easu_ps"));

  const std::string constants = patch("0179-vulkan-unchanged-constant-writes.linux.patch");
  CHECK(Has(constants, "+REXCVAR_DEFINE_BOOL(gpu_skip_unchanged_constant_writes, true"));
  CHECK(Has(constants, "+    if (frame_open_ && !unchanged) {"));
  const std::string runs = patch("0180-vulkan-float-constant-runs.linux.patch");
  CHECK(Count(runs, "+      CopyUsedFloatConstants(mapping, &regs[XE_GPU_REG_SHADER_CONSTANT_") == 2);
  const std::string index = patch("0181-vulkan-index-buffer-binding-memo.linux.patch");
  CHECK(Has(index, "+    current_index_buffer_ = {};"));
  CHECK(Has(index, "first_index, 0, 0);"));
  const std::string lto = patch("0182-gpu-plugin-thinlto.linux.patch");
  CHECK(Has(lto, "+if(NOT WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES \"Clang\")"));
  CHECK(Has(lto, "$<$<CONFIG:Release>:-flto=thin>"));
}

}  // namespace

int main() {
  TestRegisterRanges();
  TestWriteRegisterClaim();
  TestHostThreadMask();
  TestPatchWiring();
  TestCurrentReferencePatches();
  TestSubmissionDiagnostics();
  TestPortedReferenceFixes();
  TestShutdownOrder();
  TestPerformancePass();
  TestPerformanceSummary();
  TestStreamedPageRule();
  TestDrawFastPathPatches();
  TestWriteFaultCallbackFirst();
  TestPipelineStoragePendingFlush();
  TestNoNvidiaImageScaling();
  TestLinuxParityPatches();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
