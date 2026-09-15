#include "theft4_boot.h"
#include "theft4_bootstrap_audio.h"
#include "theft4_bootstrap_graphics.h"
#include "theft4_bootstrap_input.h"
#include "gta4_installer.h"
#include <rex/image_info.h>
#include <rex/cvar.h>
#include <rex/graphics/flags.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>
#include <rex/system/xthread.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

REXCVAR_DECLARE(bool, vulkan_presenter_probe_swapchain_pixels);
REXCVAR_DECLARE(std::string, render_target_path_vulkan);
REXCVAR_DECLARE(bool, vulkan_dynamic_rendering);
REXCVAR_DECLARE(std::string, gta4_transition_diagnostics);

extern const rex::PPCImageInfo PPCImageConfig;
extern "C" void gta4_transition_hooks_link_anchor();
extern "C" void theft4_ios_audio_hotpaths_link_anchor();

namespace {
// This bring-up entry is deliberately one-shot per process. A runtime owns
// guest threads and cannot be discarded while they may still be executing.
std::atomic_flag attempted = ATOMIC_FLAG_INIT;
PPCFunc* original_entry = nullptr;
theft4_boot_event_fn entry_event = nullptr;
void* entry_context = nullptr;

void ObservedEntry(PPCContext& ctx, uint8_t* base) {
    std::fprintf(stderr, "THEFT4 AOT ENTRY REACHED\n");
    std::fflush(stderr);
    entry_event(entry_context, "Recompiled GTA IV entry point is executing");
    original_entry(ctx, base);
}
}

int theft4_start_game(const char* game_directory, const char* support_directory,
                     theft4_boot_event_fn event, void* context) {
    if (!game_directory || !support_directory || !event || attempted.test_and_set()) return 2;
    try {
        // Retain the strong GTA transition wrappers in the embedded static
        // engine. They are observational and still invoke the original AOT
        // functions through their generated __imp__ entry points.
        gta4_transition_hooks_link_anchor();
        theft4_ios_audio_hotpaths_link_anchor();
        std::string reason;
        if (!gta4::install::IsInstallReady(game_directory, &reason)) {
            event(context, reason.c_str());
            return 1;
        }
        // Keep this first-execution experiment separate from existing saves.
        const auto support = std::filesystem::path(support_directory) / "startup";
        std::filesystem::create_directories(support);
        const std::string log_path = (support / "runtime.log").string();
        rex::LogConfig logging;
        logging.log_file = log_path.c_str();
        logging.log_to_console = true;
        rex::InitLogging(logging);
        // Audio timing is an opt-in engineering capture, never a normal-play
        // cost. Remove a previous bounded trace before deciding whether this
        // launch needs one so device testing cannot accumulate large CSVs.
        const auto audio_timing = support / "audio-timing";
        std::error_code audio_timing_error;
        std::filesystem::remove_all(audio_timing, audio_timing_error);
        if (const char* timing = std::getenv("THEFT4_AUDIO_TIMING");
            timing && std::string_view(timing) == "1") {
            std::filesystem::create_directories(audio_timing);
            setenv("REX_AUDIO_HANDOFF_DIR", audio_timing.c_str(), 1);
            setenv("REX_AUDIO_HANDOFF_EVENTS_ONLY", "1", 1);
            setenv("REX_AUDIO_HANDOFF_SECONDS", "600", 1);
            REXLOG_INFO("Theft4 bounded audio timing enabled: {}",
                        audio_timing.string());
        }
        // Candidate based on XeniOS's supported inline XMA mode. Set to 0 in a
        // launch environment for the dedicated-worker control.
        if (!std::getenv("THEFT4_XMA_INLINE")) setenv("THEFT4_XMA_INLINE", "1", 1);
        // Initial startup keeps the full 32-block safety buffer. After an
        // underrun, resume at eight blocks so one slow producer episode does
        // not create a quarter-second refill pause. Set to 32 for the control.
        if (!std::getenv("THEFT4_AUDIO_RECOVERY_BLOCKS"))
            setenv("THEFT4_AUDIO_RECOVERY_BLOCKS", "8", 1);
        if (const char* diagnostics = std::getenv("THEFT4_DIAGNOSTICS");
            diagnostics && std::string_view(diagnostics) == "1") {
            const auto captures = support / "frame-captures";
            std::filesystem::create_directories(captures);
            setenv("THEFT4_FRAME_CAPTURE_DIR", captures.c_str(), 1);
            REXCVAR_SET(vulkan_presenter_probe_swapchain_pixels, true);
            REXCVAR_SET(gta4_transition_diagnostics, "metadata");
            REXLOG_INFO("Theft4 bounded frame diagnostics enabled: {}", captures.string());
        }
        rex::Runtime runtime(game_directory, support / "user",
                             std::filesystem::path(game_directory) / "update",
                             support / "cache", {}, support / "marketplace", support / "saves");
        rex::RuntimeConfig config;
        config.tool_mode = false;
        config.graphics = theft4_create_bootstrap_graphics();
        config.input_factory = [](bool) { return theft4_create_bootstrap_input(); };
        config.audio_factory = [](rex::runtime::FunctionDispatcher* dispatcher) {
            return theft4_create_bootstrap_audio(dispatcher);
        };
        config.kernel_init = rex::kernel::InitializeKernel;
        // GTA IV intentionally issues invalid texture fetch constants for null
        // texture bindings. Match the compatibility behavior used by Xenia so
        // those bindings read zero instead of dropping the affected draw.
        REXCVAR_SET(gpu_allow_invalid_fetch_constants, true);
        // Keep GPU-visible resolve data coherent with guest memory. XeniOS uses
        // delayed ("fast") resolve readback by default; ReXGlue defaults to
        // disabling it, which can leave GTA IV waiting on stale scene data.
        REXCVAR_SET(readback_resolve, "fast");
        // ReXGlue's current Vulkan occlusion implementation synchronously waits
        // for every query result. Keep the established fake-result fallback as
        // the default, but allow a bounded device experiment with the real path
        // without producing a separate source variant.
        const char* real_occlusion = std::getenv("THEFT4_REAL_OCCLUSION");
        REXCVAR_SET(occlusion_query_enable,
                    real_occlusion && std::string_view(real_occlusion) == "1");
        // Opt-in comparison of the existing EDRAM implementations. Keep the
        // normal host-render-target path unchanged unless explicitly requested.
        if (const char* path = std::getenv("THEFT4_RENDER_TARGET_PATH"); path) {
            if (std::string_view(path) == "fsi") {
                REXCVAR_SET(render_target_path_vulkan, "fsi");
                // Current dynamic attachment lookup asserts host render targets.
                // FSI already supplies its own attachmentless legacy render pass.
                REXCVAR_SET(vulkan_dynamic_rendering, false);
            } else if (std::string_view(path) != "host")
                throw std::runtime_error("THEFT4_RENDER_TARGET_PATH must be host or fsi");
        }
        if (const char* legacy = std::getenv("THEFT4_LEGACY_RENDER_PASS");
            legacy && std::string_view(legacy) == "1")
            REXCVAR_SET(vulkan_dynamic_rendering, false);
        REXLOG_INFO("Theft4 graphics settings: render_targets={} resolve={} occlusion={} dynamic_rendering={}",
                    REXCVAR_GET(render_target_path_vulkan).empty() ? "host" : REXCVAR_GET(render_target_path_vulkan),
                    REXCVAR_GET(readback_resolve), REXCVAR_GET(occlusion_query_enable),
                    REXCVAR_GET(vulkan_dynamic_rendering));
        event(context, "Initializing Xbox services and registering the real AOT game functions");
        if (runtime.Setup(PPCImageConfig, std::move(config)) != 0)
            throw std::runtime_error("Xbox/AOT runtime setup failed");
        event(context, "Xbox services initialized; resolving game imports and applying TU8");
        if (runtime.LoadXexImage("game:/default.xex") != 0)
            throw std::runtime_error("Game module or Xbox import resolution failed");
        auto module = runtime.kernel_state()->GetExecutableModule();
        const auto* info = module->xex_module()->opt_execution_info();
        if (!info || info->version_value != 0x00000805)
            throw std::runtime_error("Refusing execution: loaded game is not matching TU8");
        const uint32_t title_id = runtime.kernel_state()->title_id();
        if (title_id != 0 && !runtime.cache_root().empty()) {
            event(context, "Loading the persistent GTA IV shader and pipeline cache");
            runtime.graphics_system()->InitializeShaderStorage(
                runtime.cache_root(), title_id, true);
        }
        const uint32_t address = module->entry_point();
        original_entry = runtime.function_dispatcher()->GetFunction(address);
        if (!original_entry) throw std::runtime_error("No AOT function registered for game entry point");
        entry_event = event;
        entry_context = context;
        if (!runtime.function_dispatcher()->SetFunction(address, ObservedEntry))
            throw std::runtime_error("Cannot observe the game entry point");
        auto thread = runtime.PrepareModuleLaunch();
        if (!thread) throw std::runtime_error("Cannot create the game's main Xbox thread");
        event(context, "Starting the game's main Xbox thread; waiting for first execution");
        if (thread->ResumeFromInitialSuspension() != 0)
            throw std::runtime_error("Cannot resume the game's main Xbox thread");
        // Same lifetime boundary as ReXApp: wait off the UIKit thread. Missing
        // media/UI exports stop with an explicit diagnostic rather than success.
        thread->Wait(0, 0, 0, nullptr);
        event(context, "Game main thread exited; this is not a gameplay-success result");
    } catch (const std::exception& error) {
        event(context, error.what());
        rex::ShutdownLogging();
        return 1;
    }
    rex::ShutdownLogging();
    return 0;
}
