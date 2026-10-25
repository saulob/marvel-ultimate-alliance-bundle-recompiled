// Graphics card: the setting values, the adapter list ReXGlue's d3d12_adapter index refers to, persistence and
// where the setting is registered.
// Run: mua_graphics_adapter_tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#endif

#include "graphics_adapters.h"
#include "trg/settings.h"

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

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void TestValues() {
  std::printf("values\n");
  using mua::ParseGraphicsAdapter;
  CHECK(!ParseGraphicsAdapter("auto"));
  CHECK(!ParseGraphicsAdapter(""));
  CHECK(!ParseGraphicsAdapter("-1"));
  CHECK(!ParseGraphicsAdapter("1x"));
  CHECK(!ParseGraphicsAdapter("Intel"));
  CHECK(ParseGraphicsAdapter("0") == 0);
  CHECK(ParseGraphicsAdapter("1") == 1);
  CHECK(ParseGraphicsAdapter("12") == 12);

  const std::vector<mua::GraphicsAdapter> adapters = {{0, "NVIDIA GeForce RTX 4070"}, {1, "Intel UHD Graphics 730"}};
  CHECK(mua::ResolveGraphicsAdapter("auto", adapters) == 0);
  CHECK(mua::ResolveGraphicsAdapter("", adapters) == 0);
  CHECK(mua::ResolveGraphicsAdapter("Intel", adapters) == 0);
  CHECK(mua::ResolveGraphicsAdapter("0", adapters) == 0);
  CHECK(mua::ResolveGraphicsAdapter("1", adapters) == 1);
  CHECK(mua::ResolveGraphicsAdapter("2", adapters) == 0);
  CHECK(!mua::ResolveGraphicsAdapter("1", {}));
  CHECK(!mua::ResolveGraphicsAdapter("auto", {}));
  // The index is the DXGI one, not the position in the list.
  const std::vector<mua::GraphicsAdapter> gap = {{1, "A"}, {3, "B"}};
  CHECK(mua::ResolveGraphicsAdapter("1", gap) == 1);
  CHECK(mua::ResolveGraphicsAdapter("3", gap) == 3);
  CHECK(mua::ResolveGraphicsAdapter("0", gap) == 1);
  CHECK(mua::ResolveGraphicsAdapter("auto", gap) == 1);

  auto options = mua::GraphicsAdapterOptions(adapters);
  CHECK(options.size() == 2);
  CHECK(options[0].first == "0" && options[0].second == "NVIDIA GeForce RTX 4070");
  CHECK(options[1].first == "1" && options[1].second == "Intel UHD Graphics 730");
  for (const auto& [value, label] : options) {
    CHECK(value != "auto" && label.find("Automatic") == std::string::npos);
  }
  CHECK(mua::GraphicsAdapterOptions({}).empty());
  options = mua::GraphicsAdapterOptions({{0, "Same"}, {1, "Same"}});
  CHECK(options[0].second == "Same (0)" && options[1].second == "Same (1)");
}

void TestPersistence() {
  std::printf("save and reload\n");
  const std::filesystem::path file = std::filesystem::temp_directory_path() / "mua_graphics_adapter_test.toml";
  std::filesystem::remove(file);
  {
    std::ofstream(file) << "# Marvel Ultimate Alliance player settings\nresolution_scale = 2\nmua_internal_render_scale = 75\n";
  }
  {
    trg::TextFileSettings settings(file.string());
    CHECK(!settings.Has("mua_graphics_adapter"));
    CHECK(mua::ResolveGraphicsAdapter(settings.Get("mua_graphics_adapter", ""), {{0, "A"}, {1, "B"}}) == 0);
    settings.Set("mua_graphics_adapter", "\"1\"");
    CHECK(settings.Save());
  }
  {
    trg::TextFileSettings settings(file.string());
    CHECK(settings.Get("mua_graphics_adapter") == "\"1\"");
    CHECK(settings.GetInt("mua_internal_render_scale", 100) == 75);
    settings.Set("mua_graphics_adapter", "\"auto\"");
    CHECK(settings.Save());
  }
  {
    trg::TextFileSettings settings(file.string());
    CHECK(settings.Get("mua_graphics_adapter") == "\"auto\"");
    CHECK(settings.GetInt("resolution_scale", 1) == 2);
  }
  std::filesystem::remove(file);
}

#if defined(_WIN32)
// ReXGlue opens d3d12_adapter = N as the Nth adapter of EnumAdapters1 on a CreateDXGIFactory2 factory.
void TestEnumerationMatchesRuntime() {
  std::printf("enumeration against the runtime's adapter order\n");
  const std::vector<mua::GraphicsAdapter> adapters = mua::ListGraphicsAdapters();
  for (const mua::GraphicsAdapter& adapter : adapters) {
    std::printf("  %d  %s\n", adapter.index, adapter.name.c_str());
  }
  CHECK(!adapters.empty());

  Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
  CHECK(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))));
  if (!factory) {
    return;
  }
  for (size_t i = 0; i < adapters.size(); ++i) {
    CHECK(i == 0 || adapters[i].index > adapters[i - 1].index);
    CHECK(!adapters[i].name.empty());
    CHECK(adapters[i].name != "Microsoft Basic Render Driver");
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    CHECK(factory->EnumAdapters1(UINT(adapters[i].index), &adapter) == S_OK);
    if (!adapter) {
      continue;
    }
    DXGI_ADAPTER_DESC1 desc;
    CHECK(SUCCEEDED(adapter->GetDesc1(&desc)));
    CHECK(!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE));
    const int size = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
    std::string runtime_name(size > 1 ? size_t(size - 1) : 0, '\0');
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, runtime_name.data(), size, nullptr, nullptr);
    CHECK(runtime_name == adapters[i].name);
  }
  // Every listed adapter was present, and no hardware adapter that can create a device was left out.
  UINT hardware = 0;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (UINT index = 0; factory->EnumAdapters1(index, &adapter) == S_OK; ++index, adapter.Reset()) {
    DXGI_ADAPTER_DESC1 desc;
    if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
        SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
      ++hardware;
    }
  }
  CHECK(hardware == adapters.size());
}
#endif

void TestRegistration() {
  std::printf("setting, launcher row and startup\n");
  const std::string src = MUA_SRC_DIR;
  const std::string settings = ReadFile(src + "/settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_STRING(mua_graphics_adapter, \"auto\","));
  CHECK(Has(settings, "      \"mua_graphics_adapter\",\n      \"mua_aspect_ratio\",") ||
        Has(settings, "      \"mua_graphics_adapter\",\r\n      \"mua_aspect_ratio\","));
  CHECK(Has(settings, "      kTearingSetting,\n      \"mua_graphics_adapter\",\n") ||
        Has(settings, "      kTearingSetting,\r\n      \"mua_graphics_adapter\",\r\n"));
  CHECK(Has(settings, "rex::cvar::SetFlagByName(\"d3d12_adapter\", std::to_string(*index));"));

  const std::string launcher = ReadFile(src + "/launcher.cpp");
  const size_t card = launcher.find("ui.Combo(\"Graphics card\"");
  const size_t resolution = launcher.find("ui.Combo(\"Render resolution\"");
  CHECK(card != std::string::npos && resolution != std::string::npos && card < resolution);
  CHECK(Has(launcher, "\"mua_graphics_adapter\", value.c_str(), adapters"));
  CHECK(!Has(launcher, "kGraphicsAdapterAuto") && !Has(launcher, "Automatic (Recommended)"));
  CHECK(!Has(settings, "kGraphicsAdapterAuto") && !Has(settings, "Graphics card: Automatic"));
  CHECK(Has(settings, "rex::cvar::SetFlagByName(\"mua_graphics_adapter\", std::to_string(*index));"));

  const std::string app = ReadFile(src + "/marvel_ultimate_alliance_app.h");
  const size_t load = app.find("mua::LoadPlayerSettings(");
  const size_t apply = app.find("mua::ApplyGraphicsAdapter();");
  CHECK(load != std::string::npos && apply != std::string::npos && load < apply);
}

}  // namespace

int main() {
  TestValues();
  TestPersistence();
#if defined(_WIN32)
  TestEnumerationMatchesRuntime();
#endif
  TestRegistration();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
