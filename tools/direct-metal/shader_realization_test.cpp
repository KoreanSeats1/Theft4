#include "native_shader_realization.h"
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

using namespace rex::graphics::gta4_native;
void Require(bool valid, const char* message) { if (!valid) throw std::runtime_error(message); }
int main() {
  try {
    using Failure = NativeShaderRealizationFailure;
    for (const bool stock_late : {false, true})
      for (const bool overrides : {false, true})
        for (const bool override_late : {false, true})
          for (int fail = -1; fail < 4; ++fail) {
            std::set<uint32_t> live;
            std::vector<uint32_t> created, destroyed;
            const auto result = RealizeNativeShaderVariants<uint32_t>(stock_late, overrides, override_late,
                [&](uint32_t variant) {
                  created.push_back(variant);
                  if (int(variant) == fail) return 0u;
                  const auto handle = variant + 1; live.insert(handle); return handle;
                }, [&](uint32_t handle) {
                  Require(live.erase(handle) == 1, "Destroyed an absent or already released shader");
                  destroyed.push_back(handle);
                });
            const bool stock_failed = fail == 0 || (stock_late && fail == 1);
            Require(result.stock_ready() != stock_failed, "Stock realization admitted a partial variant set");
            if (stock_failed) {
              Require(live.empty() && !result.stock_late && !result.override_early && !result.override_late,
                      "Failed stock realization leaked a module or attempted an override");
              Require(result.failure == (fail == 0 ? Failure::kStockEarly : Failure::kStockLate),
                      "Stock failure lost its reason");
              Require(created.size() == (fail == 0 ? 1u : 2u), "Creation continued after a stock failure");
              continue;
            }
            const bool override_failed = overrides && (fail == 2 || (override_late && fail == 3));
            Require(result.stock_early == 1 && result.stock_late == (stock_late ? 2u : 0u),
                    "An optional override failure changed the stock modules");
            Require(bool(result.override_early) == (overrides && !override_failed),
                    "Partial optional override was published");
            Require(bool(result.override_late) == (overrides && override_late && !override_failed),
                    "Late optional override availability changed");
            const auto expected = override_failed
                ? (fail == 2 ? Failure::kOverrideEarly : Failure::kOverrideLate) : Failure::kNone;
            Require(result.failure == expected, "Optional failure reason changed");
            std::set<uint32_t> owners;
            for (auto handle : {result.stock_early, result.stock_late, result.override_early, result.override_late})
              if (handle) owners.insert(handle);
            Require(live == owners, "A driver module escaped its returned owner");
            Require(destroyed.empty() || (override_failed && fail == 3 && destroyed == std::vector<uint32_t>{3}),
                    "Rollback destroyed the wrong shader variant");
          }
    std::cout << "Shader realization: 40 stock/override/late/failure combinations passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
