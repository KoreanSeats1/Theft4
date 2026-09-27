// Standalone ARM64 host test of the actual PPC FPSCR / hardware FPCR bridge.
#include <rex/ppc/context.h>

#include <array>
#include <cstdio>

#if !defined(__aarch64__) && !defined(_M_ARM64)
#error This test requires ARM64 hardware.
#endif

namespace {
using Register = rex::ppc::FPSCRRegister;
using Platform = rex::platform::FPSCRPlatform;

// FRINTI obeys the live FPCR rounding mode. Explicit assembly keeps the
// compiler from folding the input or assuming a fixed C++ rounding mode.
double RoundInHardware(double value) {
  double result;
  __asm__ __volatile__("frinti %d0, %d1" : "=w"(result) : "w"(value));
  return result;
}

struct RestoreFpcr {
  uint32_t saved = Platform::getcsr();
  ~RestoreFpcr() { Platform::setcsr(saved); }
};

bool Check(bool condition, const char* message, uint32_t mode) {
  if (!condition) std::fprintf(stderr, "FAIL: %s (guest mode %u)\n", message, mode);
  return condition;
}
}  // namespace

int main() {
  RestoreFpcr restore;
  bool passed = true;
  struct Case { uint32_t mode; double positive; double negative; };
  const std::array cases = {
      Case{rex::ppc::kRoundNearest, 2.0, -2.0},
      Case{rex::ppc::kRoundTowardZero, 1.0, -1.0},
      Case{rex::ppc::kRoundUp, 2.0, -1.0},
      Case{rex::ppc::kRoundDown, 1.0, -2.0},
  };
  for (const auto& test : cases) {
    Register fpscr{};
    fpscr.InitHost();
    fpscr.disableFlushMode();
    fpscr.storeFromGuest(test.mode);
    passed &= Check(RoundInHardware(1.5) == test.positive,
                    "positive hardware rounding", test.mode);
    passed &= Check(RoundInHardware(-1.5) == test.negative,
                    "negative hardware rounding", test.mode);
    passed &= Check(fpscr.loadFromHost() == test.mode,
                    "guest-host-guest mode round trip", test.mode);

    // Flush mode changes must preserve the selected rounding mode. A newly
    // initialized context must synchronize its lazy cache with the live FPCR.
    fpscr.enableFlushMode();
    Register initialized{};
    initialized.InitHost();
    passed &= Check(initialized.csr == Platform::getcsr(),
                    "new context synchronizes live FPCR", test.mode);
    passed &= Check((initialized.csr & Platform::FlushMask) == Platform::FlushMask,
                    "initialization preserves flush mode", test.mode);
    passed &= Check(initialized.loadFromHost() == test.mode,
                    "initialization preserves rounding", test.mode);
    initialized.disableFlushMode();
    passed &= Check((Platform::getcsr() & Platform::FlushMask) == 0,
                    "lazy flush disable updates hardware", test.mode);
    passed &= Check(initialized.loadFromHost() == test.mode,
                    "flush disable preserves rounding", test.mode);
  }
  if (!passed) return 1;
  std::puts("ARM64 PPC FPSCR round trips, hardware rounding, and initialization passed");
  return 0;
}
