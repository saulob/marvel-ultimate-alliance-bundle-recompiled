// vmsum3fp128 and vmsum4fp128 as the generated code computes them: products and sums in double rounded once to float,
// summed as (x + z) + y and (x + z) + (y + w), and a finite sum that overflows float gives the default NaN. Runs the
// helpers of the generated PCH; skipped when it is not there.
// Run: mua_vmsum_tests

#if __has_include(MUA_PCH_HEADER)
#include MUA_PCH_HEADER
#define MUA_HAS_PCH 1
#endif

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifdef MUA_HAS_PCH
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

constexpr uint32_t kDefaultNaN = 0x7FC00000;

// Guest elements x, y, z, w; host lane 3 - i holds element i.
simde__m128 Guest(float x, float y, float z, float w) {
  alignas(16) float host[4] = {w, z, y, x};
  return simde_mm_load_ps(host);
}

uint32_t Bits(float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float Lane(simde__m128 value, int lane) {
  alignas(16) float host[4];
  simde_mm_store_ps(host, value);
  return host[lane];
}

bool EveryLane(simde__m128 value, uint32_t bits) {
  for (int lane = 0; lane < 4; ++lane) {
    if (Bits(Lane(value, lane)) != bits) {
      return false;
    }
  }
  return true;
}

void TestSums() {
  std::printf("plain sums reach every lane and the 3-element form leaves w out\n");
  CHECK(EveryLane(rex::ppc::simde_mm_vmsum3fp(Guest(1, 2, 3, 4), Guest(5, 6, 7, 8)), Bits(38.0f)));
  CHECK(EveryLane(rex::ppc::simde_mm_vmsum4fp(Guest(1, 2, 3, 4), Guest(5, 6, 7, 8)), Bits(70.0f)));
  CHECK(EveryLane(rex::ppc::simde_mm_vmsum3fp(Guest(1, 1, 1, 1e30f), Guest(1, 1, 1, 1e30f)), Bits(3.0f)));
}

void TestOverflow() {
  std::printf("a finite sum that overflows float is the default NaN\n");
  const simde__m128 largest = Guest(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);
  CHECK(EveryLane(rex::ppc::simde_mm_vmsum3fp(largest, largest), kDefaultNaN));
  CHECK(EveryLane(rex::ppc::simde_mm_vmsum4fp(largest, largest), kDefaultNaN));
  const float result = Lane(rex::ppc::simde_mm_vmsum3fp(largest, largest), 0);
  CHECK(!(result >= 0.0f) && !(result < 0.0f));
}

void TestInfinity() {
  std::printf("infinite inputs stay infinite\n");
  const simde__m128 one = Guest(1, 1, 1, 1);
  CHECK(EveryLane(rex::ppc::simde_mm_vmsum3fp(Guest(INFINITY, 1, 1, 1), one), Bits(INFINITY)));
  CHECK(EveryLane(rex::ppc::simde_mm_vmsum4fp(Guest(1, 1, 1, -INFINITY), one), Bits(-INFINITY)));
  const float inf_times_zero = Lane(rex::ppc::simde_mm_vmsum3fp(Guest(INFINITY, 1, 1, 1), Guest(0, 1, 1, 1)), 0);
  CHECK(inf_times_zero != inf_times_zero);
}

void TestOrder() {
  std::printf("the sum order is (x + z) + y and (x + z) + (y + w)\n");
  // x = 2^100, z = -2^100, y = 1: (x + z) + y = 1, while x + (y + z) would lose the 1.
  const float big = std::ldexp(1.0f, 100);
  CHECK(EveryLane(rex::ppc::simde_mm_vmsum3fp(Guest(big, 1, -big, 0), Guest(1, 1, 1, 0)), Bits(1.0f)));
  CHECK(EveryLane(rex::ppc::simde_mm_vmsum4fp(Guest(big, 1, -big, 0), Guest(1, 1, 1, 0)), Bits(1.0f)));
  // x + z is exactly half way between two floats. Adding y and w one at a time loses both (each is under half a
  // double ulp) and the tie goes to 1.0; adding them as a pair is above half a double ulp and tips it up.
  const float half_ulp = std::ldexp(1.0f, -24);
  const float tiny = std::ldexp(3.0f, -55);
  const simde__m128 ones = Guest(1, 1, 1, 1);
  CHECK(EveryLane(rex::ppc::simde_mm_vmsum3fp(Guest(1, tiny, half_ulp, tiny), ones), Bits(1.0f)));
  CHECK(EveryLane(rex::ppc::simde_mm_vmsum4fp(Guest(1, tiny, half_ulp, tiny), ones),
                  Bits(std::nextafter(1.0f, 2.0f))));
}

void TestSingleRounding() {
  std::printf("the sum is rounded to float once, not after every addition\n");
  // 2^24 + 1 + 1 = 16777218 is a float; adding 1 twice in float stays at 2^24.
  CHECK(EveryLane(rex::ppc::simde_mm_vmsum3fp(Guest(16777216.0f, 1, 1, 0), Guest(1, 1, 1, 0)), Bits(16777218.0f)));
}

void TestGeneratedCode() {
  std::printf("no generated function still uses dpps for a dot product\n");
  namespace fs = std::filesystem;
  size_t files = 0;
  size_t helper_calls = 0;
  size_t dpps = 0;
  for (const fs::directory_entry& entry : fs::directory_iterator(MUA_GENERATED_DIR)) {
    const std::string name = entry.path().filename().string();
    if (entry.path().extension() != ".cpp" || name.find("_recomp.") == std::string::npos) {
      continue;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    const std::string all = text.str();
    ++files;
    for (size_t at = all.find("simde_mm_dp_ps"); at != std::string::npos; at = all.find("simde_mm_dp_ps", at + 1)) {
      ++dpps;
    }
    for (size_t at = all.find("rex::ppc::simde_mm_vmsum"); at != std::string::npos;
         at = all.find("rex::ppc::simde_mm_vmsum", at + 1)) {
      ++helper_calls;
    }
  }
  CHECK(files > 0);
  CHECK(dpps == 0);
  CHECK(helper_calls > 0);
}

}  // namespace
#endif

void TestFusedMultiplyAdd() {
  std::printf("vmaddfp and vnmsubfp round once\n");
  const float a = 1.0f + std::ldexp(1.0f, -13);
  const float c = -(1.0f + std::ldexp(1.0f, -12));
  const simde__m128 va = Guest(a, a, a, a);
  CHECK(EveryLane(rex::ppc::simde_mm_vmaddfp(va, va, Guest(c, c, c, c)), Bits(std::ldexp(1.0f, -26))));
  CHECK(EveryLane(rex::ppc::simde_mm_vnmsubfp(va, va, Guest(-c, -c, -c, -c)), Bits(-std::ldexp(1.0f, -26))));
  CHECK(Lane(rex::ppc::simde_mm_vmaddfp(Guest(2, 3, 4, 5), Guest(1, 1, 1, 1), Guest(1, 1, 1, 1)), 3) == 3.0f);
  std::printf("swapped multiply-add operands give a different result\n");
  const simde__m128 x = Guest(2, 2, 2, 2);
  const simde__m128 y = Guest(3, 3, 3, 3);
  const simde__m128 z = Guest(10, 10, 10, 10);
  CHECK(Lane(rex::ppc::simde_mm_vmaddfp(x, y, z), 0) == 16.0f);
  CHECK(Lane(rex::ppc::simde_mm_vmaddfp(x, z, y), 0) == 23.0f);
}

int main() {
#ifndef MUA_HAS_PCH
  std::printf("skipped: generated code is not there (%s)\n", MUA_GENERATED_DIR);
  return 0;
#else
  try {
    TestSums();
    TestOverflow();
    TestInfinity();
    TestOrder();
    TestSingleRounding();
    TestFusedMultiplyAdd();
    TestGeneratedCode();
  } catch (const std::exception& error) {
    std::printf("  FAIL exception: %s\n", error.what());
    return 1;
  }
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
#endif
}
