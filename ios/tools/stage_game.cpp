#include "gta4_installer.h"
#include <rex/logging.h>
#include <chrono>
#include <cstdio>
#include <exception>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "Usage: theft4_stage <base ISO> <update package> <NEW install directory>\n");
        return 2;
    }
    rex::InitLogging();
    int status = 2;
    try {
        const auto destination = std::filesystem::absolute(argv[3]).lexically_normal();
        // The upstream installer replaces its staging/backup paths. Only let
        // this developer utility operate inside a newly claimed directory.
        if (destination == destination.root_path() ||
            !std::filesystem::create_directory(destination)) {
            throw std::runtime_error("Destination must be a new directory with an existing parent");
        }
        gta4::install::Progress progress;
        std::jthread reporter([&](std::stop_token stop) {
            while (!stop.stop_requested()) {
                std::printf("Staging: %llu / %llu bytes\n",
                    (unsigned long long)progress.copied_bytes.load(),
                    (unsigned long long)progress.total_bytes.load());
                std::fflush(stdout);
                for (int i = 0; i < 20 && !stop.stop_requested(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        });
        const gta4::install::Selection selection{argv[1], argv[2], {}};
        auto result = gta4::install::Install(selection, destination, progress);
        if (result.success)
            result = gta4::install::VerifyInstall(destination / "game", destination / "dlc");
        reporter.request_stop();
        status = result.success ? 0 : 1;
        std::printf("Staging %s: %s\n", result.success ? "VERIFIED" : "FAILED",
            result.success ? destination.c_str() : result.error.c_str());
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Staging failed: %s\n", error.what());
    }
    rex::ShutdownLogging();
    return status;
}
