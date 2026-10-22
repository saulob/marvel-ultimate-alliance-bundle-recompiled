// What the Direct3D 12 runtime rejects when a command list closes, on a real device without a window: the scaled mip
// copies of patch 0092 and the failed-Close behaviour patch 0093 recovers from. Skipped without a D3D12 device.

#define NOMINMAX
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstdio>
#include <functional>

#include "render_scale.h"

using Microsoft::WRL::ComPtr;
using namespace mua;

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

ComPtr<ID3D12Device> g_device;

ComPtr<ID3D12Resource> Buffer(uint64_t size) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
  ComPtr<ID3D12Resource> resource;
  g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                    IID_PPV_ARGS(&resource));
  return resource;
}

ComPtr<ID3D12Resource> Texture(uint32_t width, uint32_t height, DXGI_FORMAT format, uint16_t mips) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = mips;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
  ComPtr<ID3D12Resource> resource;
  g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                    IID_PPV_ARGS(&resource));
  return resource;
}

HRESULT RecordAndClose(const std::function<void(ID3D12GraphicsCommandList*)>& record) {
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
  g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list));
  record(list.Get());
  return list->Close();
}

// The load copies a footprint of `footprint` texels from the scratch buffer into one mip, with the box patch 0092
// derives from the host mip, or without a box as before.
HRESULT CopyMip(ID3D12Resource* texture, ID3D12Resource* scratch, DXGI_FORMAT format, uint32_t level, Extent footprint,
                Extent host_mip, Extent block, bool clamp) {
  return RecordAndClose([&](ID3D12GraphicsCommandList* list) {
    D3D12_TEXTURE_COPY_LOCATION dest = {};
    dest.pResource = texture;
    dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dest.SubresourceIndex = level;
    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = scratch;
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint.Footprint = {format, footprint.width, footprint.height, 1, 256};
    const Extent copy = ScaledLoadCopyExtent(footprint, host_mip, block);
    const D3D12_BOX box = {0, 0, 0, copy.width, copy.height, 1};
    list->CopyTextureRegion(&dest, 0, 0, 0, &source, clamp ? &box : nullptr);
  });
}

void TestScaledMipCopies() {
  std::printf("scaled mip copies close only inside the destination mip\n");
  const Extent guest{1280, 720};
  const auto scratch = Buffer(1 << 20);
  for (const auto& [format, block] : {std::pair{DXGI_FORMAT_R8G8B8A8_TYPELESS, Extent{1, 1}},
                                      std::pair{DXGI_FORMAT_BC1_TYPELESS, Extent{4, 4}}}) {
    const auto texture = Texture(guest.width * 4, guest.height * 4, format, 13);
    for (uint32_t level = 9; level <= 12; ++level) {
      const Extent footprint = BlockPaddedExtent(ScaledLoadFootprint(guest, 4, level), block);
      const Extent host_mip = ScaledHostMip(guest, 4, level);
      CHECK(CopyMip(texture.Get(), scratch.Get(), format, level, footprint, host_mip, block, true) == S_OK);
      const bool fits = CopyFitsDestination(footprint, BlockPaddedExtent(host_mip, block));
      CHECK(CopyMip(texture.Get(), scratch.Get(), format, level, footprint, host_mip, block, false) ==
            (fits ? S_OK : E_INVALIDARG));
    }
  }
  // The 5120x2880 RGBA8 load of a 1280x720 render target at 4x, as in the failing submission, is valid.
  const auto target = Texture(5120, 2880, DXGI_FORMAT_R8G8B8A8_TYPELESS, 1);
  const auto large_scratch = Buffer(184549376);
  CHECK(RecordAndClose([&](ID3D12GraphicsCommandList* list) {
          D3D12_TEXTURE_COPY_LOCATION dest = {target.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
          dest.SubresourceIndex = 0;
          D3D12_TEXTURE_COPY_LOCATION source = {large_scratch.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
          source.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_TYPELESS, 5120, 2880, 1, 20480};
          list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
        }) == S_OK);
}

// Submission 8241's rejected command: a packed mip tail load into subresource 6 of a 5120x2880, 11-mip texture
// (1280x720 at 4x; GetPackedMipLevel(1280, 720) = 6, so mip 6 starts the tail). Box top 64 is mip 6's scaled guest
// offset (16 blocks * 4); box bottom 109 is (720 * 4) >> 6, the scale-first row count patch 0092 now also uses to
// size the tail's shared footprint. The old footprint height, 108, is (720 >> 6) * 4 - one row short.
void TestPackedMipTailCopy() {
  std::printf("a packed mip tail's footprint must be scale-first-sized, not the guest extent scaled afterward\n");
  const auto target = Texture(5120, 2880, DXGI_FORMAT_R8G8B8A8_TYPELESS, 11);
  const auto scratch = Buffer(184549376);
  auto CopyMip6 = [&](uint32_t footprint_height) {
    return RecordAndClose([&](ID3D12GraphicsCommandList* list) {
      D3D12_TEXTURE_COPY_LOCATION dest = {target.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
      dest.SubresourceIndex = 6;
      D3D12_TEXTURE_COPY_LOCATION source = {scratch.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
      source.PlacedFootprint.Offset = 78633984;
      source.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_TYPELESS, 80, footprint_height, 1, 512};
      const D3D12_BOX box = {0, 64, 0, 80, 109, 1};
      list->CopyTextureRegion(&dest, 0, 0, 0, &source, &box);
    });
  };
  CHECK(CopyMip6(108) == E_INVALIDARG);
  CHECK(CopyMip6(109) == S_OK);
}

void TestFailedCloseCannotReset() {
  std::printf("a list that failed to close can't be reset, a new one on the same allocator works\n");
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
  g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list));
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  list->ResourceBarrier(1, &barrier);
  CHECK(list->Close() == E_INVALIDARG);
  CHECK(allocator->Reset() == S_OK);
  CHECK(list->Reset(allocator.Get(), nullptr) == E_INVALIDARG);
  CHECK(list->Close() == E_FAIL);
  ComPtr<ID3D12GraphicsCommandList> replacement;
  CHECK(SUCCEEDED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&replacement))));
  CHECK(replacement && replacement->Close() == S_OK);
}

void TestCopyBounds() {
  std::printf("buffer copies are checked against both buffers, the 2 GiB scaled resolve buffer included\n");
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = uint64_t(1) << 31;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> scaled;
  CHECK(SUCCEEDED(g_device->CreateReservedResource(&desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                   IID_PPV_ARGS(&scaled))));
  const auto scratch = Buffer(1 << 20);
  for (const uint64_t end : {uint64_t(0x7FFFFFFF), uint64_t(0x80000000), uint64_t(0x80000001)}) {
    CHECK(RecordAndClose([&](ID3D12GraphicsCommandList* list) {
            list->CopyBufferRegion(scratch.Get(), 0, scaled.Get(), end - 4096, 4096);
          }) == (end <= desc.Width ? S_OK : E_INVALIDARG));
  }
}

}  // namespace

int main() {
  ComPtr<IDXGIFactory6> factory;
  ComPtr<IDXGIAdapter1> adapter;
  if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) ||
      FAILED(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))) ||
      FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)))) {
    std::printf("no Direct3D 12 device, skipped\n");
    return 0;
  }
  TestScaledMipCopies();
  TestPackedMipTailCopy();
  TestFailedCloseCannotReset();
  TestCopyBounds();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
