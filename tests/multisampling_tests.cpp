// Multisampling: host samples for each guest sample count and launcher choice, the Multisampling row,
// its settings key and where native_2x_msaa is set from it. The render target caches run only with a
// GPU, so their choice is mirrored by HostMsaaSamples.
// Run: mua_multisampling_tests

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "multisampling.h"

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

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(MUA_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void TestHostSamples() {
  std::printf("host samples per launcher choice\n");
  for (bool supported : {false, true}) {
    for (bool compatibility : {false, true}) {
      bool native = mua::Native2xMsaaFor(compatibility);
      CHECK(mua::HostMsaaSamples(1, native, supported) == 1);
      CHECK(mua::HostMsaaSamples(4, native, supported) == 4);
    }
  }
  CHECK(mua::HostMsaaSamples(2, mua::Native2xMsaaFor(false), true) == 2);
  CHECK(mua::HostMsaaSamples(2, mua::Native2xMsaaFor(true), true) == 4);
  CHECK(mua::HostMsaaSamples(2, mua::Native2xMsaaFor(false), false) == 4);
  // No choice draws fewer host samples than the game asks for, and native is never more.
  for (bool supported : {false, true}) {
    CHECK(mua::HostMsaaSamples(2, mua::Native2xMsaaFor(true), supported) >= 2);
    CHECK(mua::HostMsaaSamples(2, mua::Native2xMsaaFor(false), supported) <=
          mua::HostMsaaSamples(2, mua::Native2xMsaaFor(true), supported));
  }
}

void TestPathDecidesTheChoice() {
  std::printf("the choice only applies to host render targets\n");
  CHECK(!mua::HostRenderTargetsUsed("rov", true));
  CHECK(mua::HostRenderTargetsUsed("rov", false));
  CHECK(mua::HostRenderTargetsUsed("rtv", true));
  CHECK(mua::HostRenderTargetsUsed("", true));
}

void TestSetting() {
  std::printf("setting, launcher row and startup\n");
  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_BOOL(mua_msaa_compatibility, false,"));
  CHECK(Has(settings, "    \"mua_msaa_compatibility\",\n") ||
        Has(settings, "    \"mua_msaa_compatibility\",\r\n"));
  CHECK(!Has(settings, "    \"native_2x_msaa\","));
  CHECK(Has(settings, "  ApplyMultisampling();\n}") || Has(settings, "  ApplyMultisampling();\r\n}"));
  CHECK(Has(settings, "rex::cvar::SetFlagByName(\"native_2x_msaa\""));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "\"mua_msaa_compatibility\", false, \"2\\xC3\\x97 MSAA\", \"4\\xC3\\x97 MSAA (Compatibility)\""));
  CHECK(!Has(launcher, "\"native_2x_msaa\""));

  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  const std::size_t no_launcher = app.find("if (!show_launcher_) {\n      mua::ApplyMultisampling();");
  const std::size_t no_launcher_crlf =
      app.find("if (!show_launcher_) {\r\n      mua::ApplyMultisampling();");
  CHECK(no_launcher != std::string::npos || no_launcher_crlf != std::string::npos);
}

}  // namespace

int main() {
  TestHostSamples();
  TestPathDecidesTheChoice();
  TestSetting();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
