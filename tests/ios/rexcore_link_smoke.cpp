#include <rex/platform.h>
#include <rex/version.h>
#include <rex/filesystem.h>
#include <rex/memory/utils.h>
#include <cstdio>

static_assert(REX_PLATFORM_IOS == 1);
static_assert(REX_PLATFORM_MAC == 0);
static_assert(REX_PLATFORM_POSIX && REX_PLATFORM_DARWIN && REX_ARCH_ARM64);
static_assert(TARGET_OS_SIMULATOR == THEFT4_EXPECT_SIMULATOR);

int main() {
  // A link/loader check only: no guest memory, signal handlers or threads.
  std::printf("Theft4 core: %s; version=%s; page=%zu; executable=%s\n",
              REXGLUE_BUILD_PLATFORM, REXGLUE_VERSION_STRING,
              rex::memory::page_size(),
              rex::filesystem::GetExecutablePath().c_str());
  return rex::memory::IsWritableExecutableMemorySupported() ? 1 : 0;
}
