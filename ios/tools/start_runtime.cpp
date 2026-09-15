#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/diagnostics/policy.h>
#include <cstdio>

// Link-and-start the real Runtime, beyond the standalone XEX data loader.
// Kept out of default app builds until its kernel dependencies are complete.
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (!rex::diagnostics::Configure(true, "logging")) return 2;
    rex::LogConfig logging;
    logging.log_to_console = true;
    rex::InitLogging(logging);
    int status = 1;
    {
        const std::filesystem::path game(argv[1]);
        rex::Runtime runtime(game, game.parent_path() / "user", game / "update",
                             game.parent_path() / "cache");
        rex::RuntimeConfig config;
        config.tool_mode = true;
        const auto result = runtime.Setup(std::move(config));
        std::printf("Runtime setup: %08X\n", result);
        status = result == 0 ? 0 : 1;
    }
    rex::ShutdownLogging();
    return status;
}
