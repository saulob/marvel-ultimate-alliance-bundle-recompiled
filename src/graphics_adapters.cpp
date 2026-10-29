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

bool WithEffectiveGraphicsAdapter(int configured_index,
                                  const std::function<void(IDXGIAdapter1*, const GraphicsAdapterDetails&)>& use) {
  bool found = false;
  const HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  const auto create_device =
      d3d12 ? reinterpret_cast<CreateDeviceFunction>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (create_device && SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    GraphicsAdapterDetails details;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    found = FindAdapter(configured_index, create_device, factory.Get(), details, adapter);
    if (found) {
      use(adapter.Get(), details);
    }
  }
  if (d3d12) {
    FreeLibrary(d3d12);
  }
  return found;
}

bool PixelShaderEdramSupported(int configured_index) {
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

#include <dlfcn.h>

#include <algorithm>
#include <cstdlib>
#include <string_view>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

namespace mua {
namespace {

// The loader and Mesa's device selection layer already put the chosen device first then (SDK patch 0148).
bool DeviceSelectedByEnvironment() {
  for (const char* name :
       {"DRI_PRIME", "MESA_VK_DEVICE_SELECT", "VK_LOADER_DEVICE_SELECT", "__NV_PRIME_RENDER_OFFLOAD"}) {
    const char* value = std::getenv(name);
    if (value && *value) {
      return true;
    }
  }
  return false;
}

uint32_t DeviceTypeRank(VkPhysicalDeviceType type) {
  switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
      return 3;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      return 2;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
      return 1;
    default:
      return 0;
  }
}

struct VulkanDevice {
  GraphicsAdapterDetails details;
  bool fragment_shader_interlock = false;
};

// ReXGlue's condition for the render_target_path_vulkan = "fsi" path (VulkanRenderTargetCache::Initialize), with the
// shared memory binding count of SpirvShaderTranslator::GetSharedMemoryStorageBufferCountLog2.
bool FragmentShaderInterlockPathSupported(bool interlock, const VkPhysicalDeviceFeatures& features,
                                          const VkPhysicalDeviceLimits& limits) {
  const uint32_t shared_memory_bindings = limits.maxStorageBufferRange >= (UINT32_C(512) << 20)   ? 1
                                          : limits.maxStorageBufferRange >= (UINT32_C(256) << 20) ? 2
                                                                                                   : 4;
  return interlock && features.fragmentStoresAndAtomics && features.sampleRateShading &&
         limits.standardSampleLocations && shared_memory_bindings < limits.maxPerStageDescriptorStorageBuffers;
}

// The hardware devices in the order ReXGlue's Vulkan provider tries them; `index` is the one vulkan_device takes.
std::vector<VulkanDevice> EnumerateVulkanDevices() {
  std::vector<VulkanDevice> devices;
  // Left loaded: the runtime uses the same loader for its own device.
  void* loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  const auto get_proc =
      loader ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader, "vkGetInstanceProcAddr")) : nullptr;
  const auto create_instance =
      get_proc ? reinterpret_cast<PFN_vkCreateInstance>(get_proc(VK_NULL_HANDLE, "vkCreateInstance")) : nullptr;
  const auto instance_version_of = get_proc ? reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
                                                  get_proc(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"))
                                            : nullptr;
  uint32_t instance_version = VK_API_VERSION_1_0;
  if (instance_version_of && instance_version_of(&instance_version) != VK_SUCCESS) {
    instance_version = VK_API_VERSION_1_0;
  }
  // Fragment shader interlock is only reported through vkGetPhysicalDeviceFeatures2 (Vulkan 1.1).
  const bool features_2 = instance_version >= VK_API_VERSION_1_1;
  VkApplicationInfo application{};
  application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application.apiVersion = features_2 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &application;
  VkInstance instance = VK_NULL_HANDLE;
  if (!create_instance || create_instance(&create_info, nullptr, &instance) != VK_SUCCESS) {
    return devices;
  }
  const auto destroy_instance = reinterpret_cast<PFN_vkDestroyInstance>(get_proc(instance, "vkDestroyInstance"));
  const auto enumerate =
      reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get_proc(instance, "vkEnumeratePhysicalDevices"));
  const auto properties_of =
      reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get_proc(instance, "vkGetPhysicalDeviceProperties"));
  const auto features_of =
      reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(get_proc(instance, "vkGetPhysicalDeviceFeatures"));
  const auto features_2_of = features_2 ? reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
                                              get_proc(instance, "vkGetPhysicalDeviceFeatures2"))
                                        : nullptr;
  const auto extensions_of = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
      get_proc(instance, "vkEnumerateDeviceExtensionProperties"));
  auto interlock_of = [&](VkPhysicalDevice device, const VkPhysicalDeviceProperties& properties) {
    if (!features_2_of || !extensions_of || properties.apiVersion < VK_API_VERSION_1_1) {
      return false;
    }
    uint32_t extension_count = 0;
    std::vector<VkExtensionProperties> extensions;
    if (extensions_of(device, nullptr, &extension_count, nullptr) != VK_SUCCESS) {
      return false;
    }
    extensions.resize(extension_count);
    if (extensions_of(device, nullptr, &extension_count, extensions.data()) < VK_SUCCESS) {
      return false;
    }
    if (std::ranges::none_of(extensions, [](const VkExtensionProperties& extension) {
          return std::string_view(extension.extensionName) == VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME;
        })) {
      return false;
    }
    VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT interlock{};
    interlock.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &interlock;
    features_2_of(device, &features);
    return interlock.fragmentShaderSampleInterlock || interlock.fragmentShaderPixelInterlock;
  };
  uint32_t count = 0;
  std::vector<VkPhysicalDevice> physical_devices;
  if (enumerate && properties_of && features_of && enumerate(instance, &count, nullptr) == VK_SUCCESS) {
    physical_devices.resize(count);
    if (enumerate(instance, &count, physical_devices.data()) < VK_SUCCESS) {
      count = 0;
    }
    physical_devices.resize(count);
  }
  const bool by_type = !DeviceSelectedByEnvironment();
  std::vector<std::pair<VulkanDevice, uint32_t>> scored;
  for (uint32_t index = 0; index < physical_devices.size(); ++index) {
    VkPhysicalDeviceProperties properties{};
    properties_of(physical_devices[index], &properties);
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
      continue;
    }
    VkPhysicalDeviceFeatures features{};
    features_of(physical_devices[index], &features);
    const uint32_t feature_score = uint32_t(features.geometryShader) + uint32_t(features.fragmentStoresAndAtomics) +
                                   uint32_t(features.vertexPipelineStoresAndAtomics) +
                                   uint32_t(features.fillModeNonSolid);
    const uint32_t score = feature_score * 4 + (by_type ? DeviceTypeRank(properties.deviceType) : 0);
    VulkanDevice device;
    device.details = {int(index), properties.deviceName, properties.vendorID, properties.deviceID, true};
    device.fragment_shader_interlock = FragmentShaderInterlockPathSupported(
        interlock_of(physical_devices[index], properties), features, properties.limits);
    scored.push_back({std::move(device), score});
  }
  if (destroy_instance) {
    destroy_instance(instance, nullptr);
  }
  std::ranges::stable_sort(scored, std::ranges::greater{}, [](const auto& device) { return device.second; });
  for (auto& device : scored) {
    devices.push_back(std::move(device.first));
  }
  return devices;
}

const std::vector<VulkanDevice>& VulkanDevices() {
  static const std::vector<VulkanDevice> devices = EnumerateVulkanDevices();
  return devices;
}

const VulkanDevice* EffectiveVulkanDevice(int configured_index) {
  const std::vector<VulkanDevice>& devices = VulkanDevices();
  const auto configured = std::ranges::find(devices, configured_index,
                                            [](const VulkanDevice& device) { return device.details.index; });
  if (configured != devices.end()) {
    return &*configured;
  }
  return devices.empty() ? nullptr : &devices.front();
}

}  // namespace

std::vector<GraphicsAdapter> ListGraphicsAdapters() {
  std::vector<GraphicsAdapter> adapters;
  for (const VulkanDevice& device : VulkanDevices()) {
    adapters.push_back({device.details.index, device.details.name});
  }
  return adapters;
}

GraphicsAdapterDetails EffectiveGraphicsAdapter(int configured_index) {
  const VulkanDevice* device = EffectiveVulkanDevice(configured_index);
  return device ? device->details : GraphicsAdapterDetails{};
}

bool WithEffectiveGraphicsAdapter(int, const std::function<void(IDXGIAdapter1*, const GraphicsAdapterDetails&)>&) {
  return false;
}

bool PixelShaderEdramSupported(int configured_index) {
  const VulkanDevice* device = EffectiveVulkanDevice(configured_index);
  return device && device->fragment_shader_interlock;
}

}  // namespace mua

#endif
