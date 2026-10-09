#pragma once
#include <filesystem>

namespace theft4::mods {
// Build/reuse a private RPF containing the bundled table. Empty means this
// install has only loose files. Never writes to the user's game directory.
std::filesystem::path PrepareTimeCycleArchive(const std::filesystem::path& game,
    const std::filesystem::path& bundle, const std::filesystem::path& cache);
}
