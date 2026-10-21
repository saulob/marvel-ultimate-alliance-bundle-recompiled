#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "game_files.h"

namespace {

int g_failures = 0;

void Expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++g_failures;
  }
}

void PutU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    bytes[offset + i] = uint8_t(value >> (24 - 8 * i));
  }
}

std::vector<uint8_t> MakeXex(uint32_t title_id, uint32_t media_id, const std::string& pe_name) {
  std::vector<uint8_t> bytes(0x200);
  std::copy_n("XEX2", 4, bytes.begin());
  PutU32(bytes, 0x14, 2);
  PutU32(bytes, 0x18, 0x00040006);
  PutU32(bytes, 0x1C, 0x100);
  PutU32(bytes, 0x20, 0x000183FF);
  PutU32(bytes, 0x24, 0x140);
  PutU32(bytes, 0x100, media_id);
  PutU32(bytes, 0x104, 3);
  PutU32(bytes, 0x108, 3);
  PutU32(bytes, 0x10C, title_id);
  PutU32(bytes, 0x110, 0x00000101);
  PutU32(bytes, 0x140, uint32_t(pe_name.size() + 5));
  std::copy(pe_name.begin(), pe_name.end(), bytes.begin() + 0x144);
  return bytes;
}

void Write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

void TestIdentity() {
  const auto xex = MakeXex(0x415607DA, 0x5A20A6D4, "Marvel_Xenon_fnl.pe");
  const auto identity = mua::ParseXexIdentity(xex);
  Expect(identity.has_value(), "identity parsed");
  if (!identity) {
    return;
  }
  Expect(identity->title_id == 0x415607DA, "title id");
  Expect(identity->media_id == 0x5A20A6D4, "media id");
  Expect(identity->disc_number == 1 && identity->disc_count == 1, "disc numbers");
  Expect(identity->pe_name == "Marvel_Xenon_fnl.pe", "pe name");
  Expect(!mua::ParseXexIdentity(std::vector<uint8_t>(0x200)).has_value(), "zeroed header rejected");
  Expect(!mua::ParseXexIdentity(std::span<const uint8_t>(xex.data(), 0x30)).has_value(), "truncated header rejected");
}

void TestCheck(const std::filesystem::path& scratch) {
  const std::filesystem::path dir = scratch / "game";
  std::filesystem::create_directories(dir);
  Expect(mua::CheckGameFiles(dir).state == mua::GameFileState::kMissing, "empty folder is missing");

  const auto xex = MakeXex(0x415607DA, 0x5A20A6D4, "Marvel_Xenon_fnl.pe");
  Write(dir / "default.xex", xex);
  const std::array<mua::CompanionFile, 1> companions = {mua::CompanionFile{"extra.bin", 4}};
  mua::GameProfile profile{"test", "Test", 0x415607DA, 0x5A20A6D4, "Marvel_Xenon_fnl.pe", xex.size(), "", companions};
  const std::string hash = mua::Sha256Hex(dir / "default.xex");
  profile.xex_sha256 = hash;
  const std::array<mua::GameProfile, 1> profiles = {profile};

  Expect(mua::CheckGameFiles(dir, profiles).state == mua::GameFileState::kIncomplete, "companion file required");
  Write(dir / "extra.bin", std::vector<uint8_t>(4));
  const mua::GameCheck valid = mua::CheckGameFiles(dir, profiles);
  Expect(valid.state == mua::GameFileState::kValid && valid.profile == &profiles[0], "matching files are valid");

  Write(dir / "default.xex", MakeXex(0x415607DA, 0x11111111, "Marvel_Xenon_fnl.pe"));
  Expect(mua::CheckGameFiles(dir, profiles).state == mua::GameFileState::kDifferentVersion,
         "same title, other media id is a different version");
  Write(dir / "default.xex", MakeXex(0x12345678, 0x5A20A6D4, "Marvel_Xenon_fnl.pe"));
  Expect(mua::CheckGameFiles(dir, profiles).state == mua::GameFileState::kUnrecognized, "other title is unrecognized");
  Write(dir / "default.xex", std::vector<uint8_t>(0x200, 0x55));
  Expect(mua::CheckGameFiles(dir, profiles).state == mua::GameFileState::kUnrecognized, "garbage is unrecognized");
}

void TestSha256() {
  const std::filesystem::path file = std::filesystem::temp_directory_path() / "mua_sha256_test.bin";
  Write(file, {'a', 'b', 'c'});
  Expect(mua::Sha256Hex(file) == "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", "sha256 of abc");
  std::filesystem::remove(file);
}

}  // namespace

int main(int argc, char** argv) {
  const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "mua_game_files_tests";
  std::filesystem::remove_all(scratch);
  TestIdentity();
  TestSha256();
  TestCheck(scratch);
  std::filesystem::remove_all(scratch);
  if (argc > 1) {
    const mua::GameCheck check = mua::CheckGameFiles(argv[1]);
    std::printf("%s: state %d, profile %s\n", argv[1], int(check.state),
                check.profile ? std::string(check.profile->id).c_str() : "none");
    Expect(check.state == mua::GameFileState::kValid, "supplied game folder is valid");
  }
  if (g_failures) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}
