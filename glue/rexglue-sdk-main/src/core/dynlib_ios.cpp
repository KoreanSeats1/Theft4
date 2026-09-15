#include <rex/platform.h>
#include <rex/platform/dynlib.h>

static_assert(REX_PLATFORM_IOS);

#include <utility>

namespace rex::platform {

// The iOS engine uses statically registered services. Loading additional
// libraries is explicitly unsupported; callers must handle Load(false).
DynamicLibrary::~DynamicLibrary() { Close(); }
DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}
DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
  if (this != &other) {
    Close();
    handle_ = std::exchange(other.handle_, nullptr);
  }
  return *this;
}
bool DynamicLibrary::Load(const std::filesystem::path&, SymbolResolution) {
  Close();
  return false;
}
void DynamicLibrary::Close() { handle_ = nullptr; }
void* DynamicLibrary::GetRawSymbol(const char*) const { return nullptr; }

}  // namespace rex::platform
