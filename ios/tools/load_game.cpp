#include "theft4_boot.h"
#include <cstdio>
#include <filesystem>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: theft4_load <validated game directory>\n");
        return 2;
    }
    if (theft4_configure_boot_diagnostics() != 0) return 2;
    const auto support = std::filesystem::path(argv[1]).parent_path().string();
    return theft4_prepare_game(argv[1], support.c_str(), [](void*, const char* event) {
        std::puts(event);
        std::fflush(stdout);
    }, nullptr);
}
