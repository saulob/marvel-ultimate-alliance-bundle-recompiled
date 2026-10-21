#include "graphics_adapters.h"

#if defined(_WIN32)

#include <windows.h>

#include <cwchar>
#include <iterator>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace mua {
namespace {

using CreateDeviceFunction = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

std::string Utf8Name(const wchar_t* text) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) {
    return {};
  }
  std::string name(size_t(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, name.data(), size, nullptr, nullptr);
  return name;
}

// The player's choice for this program in the Windows graphics settings: 1 power saving, 2 high performance.
uint32_t UserGpuPreference() {
  wchar_t path[1024];
  const DWORD length = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
  if (length == 0 || length >= std::size(path)) {
    return 0;
  }
  wchar_t value[256];
  DWORD value_size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", path, RRF_RT_REG_SZ,
                   nullptr, value, &value_size) != ERROR_SUCCESS) {
    return 0;
  }
  const wchar_t* preference = wcsstr(value, L"GpuPreference=");
  return preference != nullptr ? uint32_t(wcstoul(preference + 14, nullptr, 10)) : 0;
}

}  // namespace

std::vector<GraphicsAdapter> ListGraphicsAdapters() {
  std::vector<GraphicsAdapter> adapters;
  const HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  const auto create_device =
      d3d12 ? reinterpret_cast<CreateDeviceFunction>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (!create_device || FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return adapters;
  }
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (UINT index = 0; factory->EnumAdapters1(index, &adapter) == S_OK; ++index, adapter.Reset()) {
    DXGI_ADAPTER_DESC1 desc;
    if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ||
        FAILED(create_device(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
      continue;
    }
    adapters.push_back({int(index), Utf8Name(desc.Description)});
  }
  return adapters;
}

namespace {

bool FindAdapter(int configured_index, CreateDeviceFunction create_device, IDXGIFactory1* factory,
                 GraphicsAdapterDetails& details, Microsoft::WRL::ComPtr<IDXGIAdapter1>& chosen_adapter) {
  Microsoft::WRL::ComPtr<IDXGIFactory6> preferred_factory;
  const bool by_preference =
      configured_index == -1 && SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&preferred_factory)));
  const DXGI_GPU_PREFERENCE preference =
      UserGpuPreference() == 1 ? DXGI_GPU_PREFERENCE_MINIMUM_POWER : DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;

  UINT index = 0;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (;; ++index, adapter.Reset()) {
    const HRESULT result =
        by_preference ? preferred_factory->EnumAdapterByGpuPreference(index, preference, IID_PPV_ARGS(&adapter))
                      : factory->EnumAdapters1(index, &adapter);
    if (result != S_OK) {
      return false;
    }
    DXGI_ADAPTER_DESC1 desc;
    if (FAILED(adapter->GetDesc1(&desc))) {
      continue;
    }
    const bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    const bool supported =
        SUCCEEDED(create_device(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr));
    const bool chosen = configured_index >= 0 ? int(index) == configured_index && supported
                        : configured_index == -2 ? software && supported
                                                 : !software && supported;
    if (!chosen) {
      continue;
    }
    details.index = int(index);
    details.name = Utf8Name(desc.Description);
    details.vendor_id = desc.VendorId;
    details.device_id = desc.DeviceId;
    details.known = true;
    chosen_adapter = adapter;
    return true;
  }
}

}  // namespace

GraphicsAdapterDetails EffectiveGraphicsAdapter(int configured_index) {
  GraphicsAdapterDetails details;
  const HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  const auto create_device =
      d3d12 ? reinterpret_cast<CreateDeviceFunction>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (create_device && SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    FindAdapter(configured_index, create_device, factory.Get(), details, adapter);
  }
  if (d3d12) {
    FreeLibrary(d3d12);
  }
  return details;
}

bool RasterizerOrderedViewsSupported(int configured_index) {
  bool supported = false;
  const HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  const auto create_device =
      d3d12 ? reinterpret_cast<CreateDeviceFunction>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (create_device && SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    GraphicsAdapterDetails details;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    if (FindAdapter(configured_index, create_device, factory.Get(), details, adapter) &&
        SUCCEEDED(create_device(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) &&
        SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) {
      supported = options.ROVsSupported != FALSE;
    }
  }
  if (d3d12) {
    FreeLibrary(d3d12);
  }
  return supported;
}

}  // namespace mua

#else

namespace mua {

std::vector<GraphicsAdapter> ListGraphicsAdapters() {
  return {};
}

GraphicsAdapterDetails EffectiveGraphicsAdapter(int) {
  return {};
}

bool RasterizerOrderedViewsSupported(int) {
  return false;
}

}  // namespace mua

#endif
