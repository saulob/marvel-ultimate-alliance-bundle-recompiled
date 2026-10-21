// Writes the decrypted, decompressed guest image of an Xbox 360 XEX to a file,
// loading it the same way `rexglue codegen` does (runtime in tool mode).
//   dump_image <game_root> <xex name> <out.bin>
// Prints "base size" so analysis scripts know where the image starts.
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>

using rex::X_STATUS;

int main(int argc, char** argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: dump_image <game_root> <xex name> <out.bin>\n");
    return 2;
  }
  auto runtime = std::make_unique<rex::Runtime>(argv[1]);
  if (runtime->Setup(rex::RuntimeConfig{.kernel_init = rex::kernel::InitializeKernel, .tool_mode = true}) !=
      X_STATUS_SUCCESS) {
    std::fprintf(stderr, "runtime setup failed\n");
    return 1;
  }
  if (runtime->LoadXexImage(std::string("game:\\") + argv[2]) != X_STATUS_SUCCESS) {
    std::fprintf(stderr, "XEX load failed\n");
    return 1;
  }
  auto* xex = runtime->kernel_state()->GetExecutableModule()->xex_module();
  const uint32_t base = xex->base_address(), size = xex->image_size();
  std::ofstream out(argv[3], std::ios::binary);
  out.write(reinterpret_cast<const char*>(runtime->virtual_membase() + base), size);
  std::printf("0x%08X 0x%08X\n", base, size);
  return out ? 0 : 1;
}
