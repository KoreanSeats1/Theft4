#include "../../ios/bridge/theft4_bc_compatibility.h"
#include <stdexcept>
#include <iostream>
static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    for (uint32_t limit : {0u, 16u, 26u, 32u, 1024u}) {
        Check(!theft4_bc_needs_preparation(true), "BC-capable GPU requires preparation");
        Check(theft4_bc_cached_sampler_capacity(true, limit, 26) == 26,
              "BC-capable GPU changed the build94 cached sampler layout");
        for (bool older : {false, true}) for (bool hdr : {false, true})
            Check(!theft4_bc_needs_direct_present(true, older, limit, hdr),
                  "BC-capable GPU changed its presentation path");
    }
    Check(theft4_bc_needs_preparation(false), "Unsupported BC GPU skipped preparation");
    Check(theft4_bc_cached_sampler_capacity(false, 16, 26) == 16,
          "Older GPU exceeded its sampler limit");
    Check(theft4_bc_cached_sampler_capacity(false, 32, 26) == 26,
          "Larger sampler GPU unnecessarily compacted its layout");
    Check(theft4_bc_needs_direct_present(false, true, 16, false),
          "A12Z compatibility presentation was not selected");
    Check(!theft4_bc_needs_direct_present(false, true, 16, true) &&
          !theft4_bc_needs_direct_present(false, false, 16, false) &&
          !theft4_bc_needs_direct_present(false, true, 32, false) &&
          !theft4_bc_needs_direct_present(false, true, 0, false),
          "Presentation compatibility escaped its tested capability gate");
    std::cout << "BC capability policy and modern-path preservation: PASS\n";
}
