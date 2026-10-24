// Generic fixes in the patched ReXGlue source: the guest clock, XMA context updates, the SDL audio credit, ImGui mouse
// events, content results, in-place overwrites, the dot product codegen and zero-size profile settings. Reads the
// staged source; skipped when it is not there.
// Run: mua_sdk_fix_tests

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace fs = std::filesystem;

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

std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  std::string result = text.str();
  std::erase(result, '\r');
  return result;
}

bool Has(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

std::string Body(const std::string& text, const std::string& signature) {
  const size_t start = text.find(signature);
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = text.find("\n}\n", start);
  return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

void TestGuestClock(const fs::path& source) {
  std::printf("the guest clock counts from a base instead of adding a rounded delta on every read\n");
  const std::string clock = Read(source / "src/core/clock.cpp");
  CHECK(!clock.empty());
  CHECK(!Has(clock, "last_guest_tick_count_ +="));
  CHECK(!Has(clock, "last_host_tick_count_ ="));
  CHECK(Has(clock, "struct ClockSegment"));
  CHECK(Has(clock, "guest_tick_floor_"));
}

void TestXmaContext(const fs::path& source) {
  std::printf("the decoder publishes only its own XMA context bits and the layout follows the hardware\n");
  const std::string header = Read(source / "include/rex/audio/xma/context.h");
  CHECK(Has(header, "static void UpdateWord("));
  CHECK(Has(header, "static uint32_t LoadWord("));
  CHECK(Has(header, "uint32_t loop_subframe_start : 2;"));
  CHECK(Has(header, "uint32_t loop_subframe_end : 3;"));
  CHECK(!Has(header, "unk_dword_1_a"));

  const std::string context = Read(source / "src/audio/xma_context.cpp");
  const std::string merged = Body(context, "void XmaContext::StoreContextMerged(");
  CHECK(!merged.empty());
  CHECK(Has(merged, "XMA_CONTEXT_DATA::UpdateWord("));
  CHECK(!Has(merged, "fresh."));
  const std::string work = Body(context, "bool XmaContext::Work()");
  const size_t locked = work.find("std::lock_guard<std::mutex> lock(lock_);");
  const size_t rechecked = work.find("if (!is_allocated() || !is_enabled())", locked);
  const size_t disabled = work.find("set_is_enabled(false);", locked);
  CHECK(locked != std::string::npos && rechecked != std::string::npos && disabled != std::string::npos);
  CHECK(rechecked < disabled);
}

void TestVmaddOperandOrder(const fs::path& source) {
  std::printf("the multiply-add operand order follows Xenon\n");
  const std::string table = Read(source / "thirdparty/disasm/ppc-dis.c");
  CHECK(Has(table, "{ VD128, VA128, VB128, VS128 }, PPC_INST_VMADDFP128"));
  CHECK(Has(table, "{ VD128, VA128, VS128, VB128 }, PPC_INST_VMADDCFP128"));
  CHECK(Has(table, "{ VD, VA, VC, VB }, PPC_INST_VMADDFP }"));
  CHECK(Has(table, "{ VD, VA, VC, VB }, PPC_INST_VNMSUBFP }"));
  const std::string builder = Read(source / "src/codegen/builders/vector.cpp");
  CHECK(Has(builder, "rex::ppc::simde_mm_vmaddfp(simde_mm_load_ps({}.f32), \"\n      \"simde_mm_load_ps({}.f32), simde_mm_load_ps({}.f32)));\","));
}

void TestXmaStallGuards(const fs::path& source) {
  std::printf("the XMA context keeps draining a partly delivered frame\n");
  const std::string context = Read(source / "src/audio/xma_context.cpp");
  CHECK(Has(context, "current_frame_remaining_subframes_ == 0) {\n      break;"));
  CHECK(Has(context, "remaining_subframe_blocks_in_output_buffer_ == 0 && output_rb.empty()"));
}

void TestSdlAudioCredit(const fs::path& source) {
  std::printf("a rejected SDL write still returns the audio credit\n");
  const std::string driver = Read(source / "src/audio/sdl/sdl_audio_driver.cpp");
  const std::string callback = Body(driver, "void SDLAudioDriver::SDLCallback(");
  const size_t write = callback.find("const bool submitted = SDL_PutAudioStreamData(");
  const size_t release = callback.find("semaphore_->Release(1, nullptr)", write);
  const size_t leave = callback.find("if (!submitted) {\n        break;", release);
  CHECK(write != std::string::npos && release != std::string::npos && leave != std::string::npos);
  CHECK(write < release && release < leave);
}

void TestImGuiMouse(const fs::path& source) {
  std::printf("ImGui gets mouse input as queued events\n");
  const std::string drawer = Read(source / "src/ui/imgui_drawer.cpp");
  CHECK(!Has(drawer, "io.MouseDown["));
  CHECK(!Has(drawer, "io.MousePos"));
  CHECK(!Has(drawer, "ImGui::IsAnyMouseDown()"));
  CHECK(Has(drawer, "io.AddMouseButtonEvent(button, true);"));
  CHECK(Has(drawer, "io.AddMouseButtonEvent(button, false);"));
  CHECK(Has(drawer, "io.AddMouseWheelEvent("));
  CHECK(Has(drawer, "io.AddMousePosEvent("));
}

void TestContentAndOverwrite(const fs::path& source) {
  std::printf("content results are compared with X_ERROR_SUCCESS and overwrites truncate in place\n");
  const std::string content = Read(source / "src/kernel/xam/xam_content.cpp");
  const size_t created = content.find("result = content_manager->CreateContent(");
  CHECK(created != std::string::npos);
  CHECK(content.find("if (result == X_ERROR_SUCCESS) {", created) < content.find("WriteContentHeaderFile(", created));
  CHECK(!Has(content, "if (license_mask_ptr && XSUCCEEDED(result))"));
  const std::string manager = Read(source / "src/system/xam/content_manager.cpp");
  CHECK(Has(manager, "content_data) == X_ERROR_SUCCESS"));

  const std::string vfs = Read(source / "src/filesystem/virtual_file_system.cpp");
  const size_t overwrite = vfs.find("case FileDisposition::kOverwriteIf:");
  const size_t truncate = vfs.find("if (!entry->Truncate()) {", overwrite);
  const size_t remove = vfs.find("if (!entry->Delete()) {", truncate);
  CHECK(overwrite != std::string::npos && truncate != std::string::npos && remove != std::string::npos);
  CHECK(overwrite < truncate && truncate < remove);
  const std::string host = Read(source / "src/filesystem/devices/host_path_entry.cpp");
  CHECK(Has(host, "file_handle->SetLength(0)"));
}

void TestDotProductCodegen(const fs::path& source) {
  std::printf("vmsum3fp128 and vmsum4fp128 are generated as double precision dot products\n");
  const std::string builders = Read(source / "src/codegen/builders/vector.cpp");
  const std::string three = Body(builders, "bool build_vmsum3fp128(");
  const std::string four = Body(builders, "bool build_vmsum4fp128(");
  CHECK(Has(three, "rex::ppc::simde_mm_vmsum3fp(") && !Has(three, "simde_mm_dp_ps"));
  CHECK(Has(four, "rex::ppc::simde_mm_vmsum4fp(") && !Has(four, "simde_mm_dp_ps"));
  const std::string pch = Read(source / "resources/templates/codegen/pch_h.inja");
  CHECK(Has(pch, "inline simde__m128 simde_mm_vmsum3fp("));
  CHECK(Has(pch, "inline simde__m128 simde_mm_vmsum4fp("));
}

}  // namespace

// The buffer of patches/rexglue-sdk/0142-xam-zero-size-profile-settings.patch.
std::vector<uint8_t> ProfileSettingBytes(const uint8_t* data, size_t size) {
  std::vector<uint8_t> bytes(size);
  if (data && size) {
    std::memcpy(bytes.data(), data, size);
  }
  return bytes;
}

struct DestructionProbe {
  explicit DestructionProbe(int* destroyed) : destroyed(destroyed) {}
  virtual ~DestructionProbe() = default;
  int* destroyed;
};

struct DerivedProbe : DestructionProbe {
  using DestructionProbe::DestructionProbe;
  ~DerivedProbe() override { ++*destroyed; }
};

void TestZeroSizeProfileSettings(const fs::path& patches, const fs::path& source) {
  std::printf("zero-size binary profile settings skip the memcpy and settings have a virtual destructor\n");
  const uint8_t guest[4] = {1, 2, 3, 4};
  CHECK(ProfileSettingBytes(guest, 0).empty());
  CHECK(ProfileSettingBytes(nullptr, 0).empty());
  CHECK((ProfileSettingBytes(guest, 4) == std::vector<uint8_t>{1, 2, 3, 4}));
  CHECK((ProfileSettingBytes(nullptr, 3) == std::vector<uint8_t>(3, 0)));

  static_assert(std::has_virtual_destructor_v<DestructionProbe>);
  int destroyed = 0;
  std::unique_ptr<DestructionProbe> setting = std::make_unique<DerivedProbe>(&destroyed);
  setting = std::make_unique<DerivedProbe>(&destroyed);
  CHECK(destroyed == 1);
  setting.reset();
  CHECK(destroyed == 2);

  const std::string patch = Read(patches / "0142-xam-zero-size-profile-settings.patch");
  CHECK(!patch.empty());
  CHECK(Has(patch, "\ndiff --git a/src/kernel/xam/xam_user.cpp b/") &&
        Has(patch, "\ndiff --git a/include/rex/system/xam/user_profile.h b/"));
  CHECK(Has(patch, "+        std::vector<uint8_t> bytes(binary_size);\n"));
  CHECK(Has(patch, "+        if (setting.data.binary.ptr && binary_size) {\n           std::memcpy(bytes.data(), binary_ptr, binary_size);"));
  CHECK(Has(patch, "-          bytes.resize(binary_size);\n"));
  CHECK(Has(patch, "+    virtual ~Setting() = default;\n"));

  const std::string header = Read(source / "include/rex/system/xam/user_profile.h");
  if (Has(header, "virtual ~Setting() = default;")) {
    const std::string user = Read(source / "src/kernel/xam/xam_user.cpp");
    const size_t write = user.find("case UserProfile::Setting::Type::BINARY: {");
    const size_t sized = user.find("std::vector<uint8_t> bytes(binary_size);", write);
    const size_t guard = user.find("if (setting.data.binary.ptr && binary_size) {", sized);
    const size_t copy = user.find("std::memcpy(bytes.data(), binary_ptr, binary_size);", guard);
    const size_t add = user.find("AddSetting(", copy);
    CHECK(write != std::string::npos && write < sized && sized < guard && guard < copy && copy < add);
    CHECK(header.find("virtual ~Setting()") < header.find("virtual void Append(", header.find("struct Setting {")));
  }
}

int main() {
  const fs::path source = MUA_SDK_SOURCE_DIR;
  TestZeroSizeProfileSettings(source / "../../patches/rexglue-sdk", source);
  if (!fs::exists(source / "src/core/clock.cpp")) {
    std::printf("skipped: the patched ReXGlue source is not staged (%s)\n", source.string().c_str());
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
  }
  TestGuestClock(source);
  TestXmaContext(source);
  TestSdlAudioCredit(source);
  TestVmaddOperandOrder(source);
  TestXmaStallGuards(source);
  TestImGuiMouse(source);
  TestContentAndOverwrite(source);
  TestDotProductCodegen(source);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
