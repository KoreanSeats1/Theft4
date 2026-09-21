#include "theft4_boot.h"
#include "gta4_installer.h"
#include <rex/logging.h>
#include <rex/diagnostics/policy.h>
#include <rex/system/xmemory.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xex_module.h>
#include <rex/runtime.h>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <string_view>
#include <vector>

int theft4_configure_boot_diagnostics(void) {
    const char* enabled = std::getenv("THEFT4_DIAGNOSTICS");
    const bool detailed = enabled && std::string_view(enabled) == "1";
    // Keep the bounded profiler available for the graph gesture or the next
    // launch capture switch. Collection remains dormant until armed.
    return rex::diagnostics::Configure(
               true,
               detailed
                   ? "logging,transition,audio,vulkan,presenter,guest-hooks,native-profiler"
                   : "logging,native-profiler")
               ? 0
               : 1;
}

static std::vector<uint8_t> ReadExecutable(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (size < sizeof(rex::xex2_header) || size > 64 * 1024 * 1024)
        throw std::runtime_error("Invalid executable size");
    std::vector<uint8_t> bytes(size);
    std::ifstream file(path, std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Cannot read executable from app sandbox");
    return bytes;
}

int theft4_prepare_game(const char* game_directory, const char* support_directory,
                       theft4_boot_event_fn event, void* context) {
    static std::atomic_flag running = ATOMIC_FLAG_INIT;
    if (!game_directory || !support_directory || !event || running.test_and_set()) return 2;
    struct Clear { ~Clear() { running.clear(); } } clear;
    int status = 1;
    try {
        const std::filesystem::path support(support_directory);
        const auto log_path = (support / "runtime.log").string();
        rex::LogConfig logging;
        logging.log_file = log_path.c_str();
        logging.log_to_console = true;
        rex::InitLogging(logging);
        event(context, "Checking installed game and TU8");
        std::string reason;
        if (!gta4::install::IsInstallReady(game_directory, &reason)) {
            event(context, reason.c_str());
        } else {
            event(context, "Game and TU8 verified on this device");
            event(context, "Initializing Xbox runtime, memory, and filesystem");
            const std::filesystem::path game(game_directory);
            rex::Runtime runtime(game, support / "user", game / "update", support / "cache",
                                 {}, support / "marketplace", support / "saves");
            rex::RuntimeConfig config;
            config.tool_mode = true;
            if (runtime.Setup(std::move(config)) == 0) {
                event(context, "Runtime and kernel host worker initialized; loading executable");
                // The existing XEX loader deliberately splits data loading /
                // patching from LoadContinue (kernel import resolution). Do
                // the former here; LoadContinue still requires the full HLE
                // export modules, not just the initialized KernelState.
                rex::runtime::XexModule base(runtime.function_dispatcher(), runtime.kernel_state());
                rex::runtime::XexModule patch(runtime.function_dispatcher(), runtime.kernel_state());
                const auto base_bytes = ReadExecutable(std::filesystem::path(game_directory) / "default.xex");
                const auto patch_bytes = ReadExecutable(std::filesystem::path(game_directory) / "default.xexp");
                if (!base.Load("default.xex", "game:/default.xex", base_bytes.data(), base_bytes.size()))
                    throw std::runtime_error("Xbox executable data load failed");
                event(context, "Xbox executable loaded; applying TU8 in memory");
                if (!patch.Load("default.xexp", "game:/default.xexp", patch_bytes.data(), patch_bytes.size()))
                    throw std::runtime_error("Xbox title update load failed");
                const int patch_status = patch.ApplyPatch(&base);
                if (patch_status != 0)
                    throw std::runtime_error("Xbox title update application failed: " + std::to_string(patch_status));
                const auto* info = base.opt_execution_info();
                if (!info || info->version_value != 0x00000805 || !base.is_valid_executable())
                    throw std::runtime_error("Patched Xbox executable verification failed");
                event(context, "TU8 loaded in the runtime; HLE exports and graphics still required before game execution");
                status = 0;
            } else {
                event(context, "Xbox runtime initialization failed; see runtime log");
            }
        }
    } catch (const std::exception& error) {
        event(context, error.what());
    }
    rex::ShutdownLogging();
    if (status == 0)
        event(context, "Runtime initialized and TU8 loaded successfully; stopped before game execution");
    return status;
}
