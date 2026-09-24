#include "theft4_boot.h"
#include "theft4_bootstrap_audio.h"
#include "theft4_bootstrap_graphics.h"
#include "theft4_bootstrap_input.h"
#include "theft4_metal_presenter.h"
#include "theft4_motion_blur.h"
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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

REXCVAR_DECLARE(bool, vulkan_presenter_probe_swapchain_pixels);
REXCVAR_DECLARE(std::string, render_target_path_vulkan);
REXCVAR_DECLARE(bool, vulkan_dynamic_rendering);
REXCVAR_DECLARE(bool, vulkan_submit_on_primary_buffer_end);
REXCVAR_DECLARE(bool, execute_unclipped_draw_vs_on_cpu);
REXCVAR_DECLARE(bool, execute_unclipped_draw_vs_on_cpu_with_scissor);
REXCVAR_DECLARE(bool, draw_extent_estimator_diagnostics);
REXCVAR_DECLARE(bool, vulkan_ownership_transfer_diagnostics);
REXCVAR_DECLARE(bool, vulkan_transfer_in_draw_pass);
REXCVAR_DECLARE(bool, vulkan_tight_render_area);
REXCVAR_DECLARE(std::string, gta4_transition_diagnostics);
#ifdef THEFT4_HAS_GTA4_NATIVE_BACKEND
REXCVAR_DECLARE(uint32_t, gta4_native_frames_in_flight);
REXCVAR_DECLARE(bool, gta4_native_texture_content_cache);
REXCVAR_DECLARE(bool, gta4_native_sparse_texture_walks);
REXCVAR_DECLARE(bool, gta4_native_worker_stall_attribution);
REXCVAR_DECLARE(bool, gta4_native_async_pipeline_no_wait);
REXCVAR_DECLARE(bool, gta4_native_pipeline_prewarm);
REXCVAR_DECLARE(bool, gta4_profile_native_detailed_gpu);
REXCVAR_DECLARE(bool, gta4_profile_native_detailed_cpu);
REXCVAR_DECLARE(bool, gta4_profile_native_autostart);
REXCVAR_DECLARE(uint32_t, gta4_profile_native_gpu_query_budget);
REXCVAR_DECLARE(uint32_t, gta4_profile_native_interval);
REXCVAR_DECLARE(uint32_t, gta4_profile_native_samples);
REXCVAR_DECLARE(std::string, gta4_anisotropic_filtering);
REXCVAR_DECLARE(int32_t, video_mode_width);
REXCVAR_DECLARE(int32_t, video_mode_height);
REXCVAR_DECLARE(std::string, gta4_native_upscaler);
REXCVAR_DECLARE(std::string, gta4_fsr1_quality);
REXCVAR_DECLARE(std::string, present_effect);
REXCVAR_DECLARE(double, present_fsr_sharpness_reduction);
REXCVAR_DECLARE(double, gta4_fsr1_sharpness_reduction);
REXCVAR_DECLARE(uint32_t, gta4_shadow_map_base_size);
REXCVAR_DECLARE(double, gta4_shadow_distance_scale);
REXCVAR_DECLARE(std::string, gta4_reflection_resolution);
REXCVAR_DECLARE(std::string, gta4_aspect_ratio);
REXCVAR_DECLARE(std::string, gta4_native_anti_aliasing);
REXCVAR_DECLARE(bool, gta4_force_highest_lod);
REXCVAR_DECLARE(double, gta4_lod_selection_distance_scale);
REXCVAR_DECLARE(double, gta4_draw_distance_scale);
REXCVAR_DECLARE(uint32_t, gta4_drawable_reference_limit);
#endif

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
        if (const char* flight = std::getenv("THEFT4_GPU_FLIGHT_TRACE");
            flight && std::string_view(flight) == "1") {
            if (!std::getenv("REX_GPU_FLIGHT_TRACE_PATH")) {
                const auto trace_id = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                const auto flight_path =
                    support / ("gpu-flight-" + std::to_string(trace_id) + ".jsonl");
                setenv("REX_GPU_FLIGHT_TRACE_PATH", flight_path.c_str(), 1);
                REXLOG_INFO("Theft4 failure-triggered GPU flight trace armed: {}",
                            flight_path.string());
            } else {
                REXLOG_INFO("Theft4 failure-triggered GPU flight trace using supplied path");
            }
        }
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
#ifdef THEFT4_HAS_GTA4_NATIVE_BACKEND
        // Keep detailed diagnostics opt-in. A19 phones skip speculative
        // prewarming after the observed deep render-worker backlog; pipeline
        // creation at the authoritative recording point remains enabled.
        const char* device_profile_value = std::getenv("THEFT4_DEVICE_PROFILE");
        const std::string_view device_profile =
            device_profile_value ? device_profile_value : "generic";
        const bool a19_profile = device_profile == "a19";
        REXCVAR_SET(gta4_native_pipeline_prewarm, !a19_profile);
        REXCVAR_SET(gta4_native_sparse_texture_walks, a19_profile);
        REXCVAR_SET(gta4_profile_native_detailed_gpu, false);
        REXCVAR_SET(gta4_profile_native_detailed_cpu, false);
        REXCVAR_SET(gta4_profile_native_autostart, false);
        REXLOG_INFO(
            "Theft4 device profile: {} pipeline-prewarm={} sparse-texture-walks={} detailed-profile=false "
            "profile-autostart=false",
            device_profile, !a19_profile, a19_profile);

        // The launch policy independently selects scene and drawable sizes.
        // Native hooks derive scene = logical video / 1.5 for FSR Quality.
        // The CAMetalLayer is already sized before swapchain creation.
        const int motion_blur = theft4::motion_blur::ParseSetting(std::getenv("THEFT4_MOTION_BLUR"));
        if (motion_blur < 0)
            throw std::runtime_error("THEFT4_MOTION_BLUR must be 0 or 1");
        const char* blur_trace = std::getenv("THEFT4_MOTION_BLUR_TRACE");
        theft4::motion_blur::Configure(motion_blur != 0,
            blur_trace && std::string_view(blur_trace) == "1");
        REXLOG_INFO("Theft4 motion blur: {} (stock composite-pass selection)",
                    motion_blur ? "on" : "off");
        const char* depth_of_field_value = std::getenv("THEFT4_DEPTH_OF_FIELD");
        if (depth_of_field_value && std::string_view(depth_of_field_value) != "0" &&
            std::string_view(depth_of_field_value) != "1")
            throw std::runtime_error("THEFT4_DEPTH_OF_FIELD must be 0 or 1");
        REXLOG_INFO("Theft4 depth of field: {} (native title multiplier)",
                    depth_of_field_value && std::string_view(depth_of_field_value) == "0" ? "off" : "on");
        const auto output = theft4_metal_get_output_policy();
        // Theft4 is full-screen by default: the 3D camera follows the actual
        // iPhone/iPad drawable. GTA IV's aspect hooks independently preserve
        // the authored HUD geometry and safe layout.
        REXCVAR_SET(gta4_aspect_ratio, std::string("auto"));
        // Use the policy's logical size, not the physical drawable: its FSR
        // ratio produces exactly the selected 720p, 900p or 1080p scene.
        REXCVAR_SET(video_mode_width, int32_t(output.video_width));
        REXCVAR_SET(video_mode_height, int32_t(output.video_height));
        REXCVAR_SET(gta4_native_upscaler, output.fsr1 ? "fsr1" : "native");
        REXCVAR_SET(gta4_fsr1_quality, "quality");
        REXCVAR_SET(present_effect, output.fsr1 ? "fsr" : "bilinear");
        REXCVAR_SET(present_fsr_sharpness_reduction,
                    REXCVAR_GET(gta4_fsr1_sharpness_reduction));
        REXLOG_INFO("Theft4 output policy: render={}x{} output={}x{} aspect=native-display "
                    "hud=authored-16:9 upscaler={} quality=quality sharpness-reduction={} "
                    "fps-counter=content-sequence",
                    output.render_width, output.render_height,
                    output.output_width, output.output_height,
                    output.fsr1 ? "fsr1" : "native",
                    REXCVAR_GET(present_fsr_sharpness_reduction));
        // A same-scene iPad A/B showed that forced 8x filtering introduced
        // visible frame-time instability in heavy views for only a marginal
        // image-quality gain. Use 4x as the balanced iOS default and retain an
        // explicit launch override for controlled device comparisons.
        const char* anisotropy_override = std::getenv("THEFT4_ANISOTROPY");
        const std::string_view anisotropy =
            anisotropy_override ? anisotropy_override : "4x";
        if (anisotropy != "1x" && anisotropy != "2x" &&
            anisotropy != "4x" && anisotropy != "8x" &&
            anisotropy != "16x") {
            throw std::runtime_error(
                "THEFT4_ANISOTROPY must be 1x, 2x, 4x, 8x, or 16x");
        }
        REXCVAR_SET(gta4_anisotropic_filtering, std::string(anisotropy));
        REXLOG_INFO("Theft4 material anisotropic filtering set to {} ({})",
                    anisotropy,
                    anisotropy_override ? "launch override" : "iOS default");

        // The launcher exposes established native-renderer controls. Preserve
        // the existing iOS values when an older install has no saved selection
        // so adding the menu does not silently make a performance run heavier.
        const char* shadow_override = std::getenv("THEFT4_SHADOW_QUALITY");
        const std::string_view shadow = shadow_override ? shadow_override : "original";
        uint32_t shadow_map_size = 0;
        double shadow_distance = 0.0;
        if (shadow == "optimized") {
            shadow_map_size = 128;
            shadow_distance = 0.75;
        } else if (shadow == "original") {
            shadow_map_size = 256;
            shadow_distance = 1.0;
        } else if (shadow == "enhanced") {
            shadow_map_size = 512;
            shadow_distance = 1.0;
        } else if (shadow == "ultra") {
            shadow_map_size = 1024;
            shadow_distance = 1.5;
        } else {
            throw std::runtime_error(
                "THEFT4_SHADOW_QUALITY must be optimized, original, enhanced, or ultra");
        }
        REXCVAR_SET(gta4_shadow_map_base_size, shadow_map_size);
        REXCVAR_SET(gta4_shadow_distance_scale, shadow_distance);

        const char* draw_distance_override = std::getenv("THEFT4_DRAW_DISTANCE");
        const std::string_view draw_distance =
            draw_distance_override ? draw_distance_override : "1";
        double draw_distance_scale = 0.0;
        uint32_t drawable_reference_limit = 0;
        if (draw_distance == "0.70") {
            draw_distance_scale = 0.70;
            drawable_reference_limit = 13000;
        } else if (draw_distance == "1") {
            draw_distance_scale = 1.0;
            drawable_reference_limit = 13000;
        } else if (draw_distance == "2") {
            draw_distance_scale = 2.0;
            drawable_reference_limit = 17000;
        } else if (draw_distance == "3") {
            draw_distance_scale = 3.0;
            // Matches the established desktop 3x default without expanding
            // the heavy-area producer workload beyond the proven capacity.
            drawable_reference_limit = 20000;
        } else {
            throw std::runtime_error("THEFT4_DRAW_DISTANCE must be 0.70, 1, 2, or 3");
        }
        REXCVAR_SET(gta4_draw_distance_scale, draw_distance_scale);
        REXCVAR_SET(gta4_drawable_reference_limit, drawable_reference_limit);

        const char* highest_lod_override = std::getenv("THEFT4_FORCE_HIGHEST_LOD");
        const std::string_view highest_lod =
            highest_lod_override ? highest_lod_override : "0";
        if (highest_lod != "0" && highest_lod != "1") {
            throw std::runtime_error("THEFT4_FORCE_HIGHEST_LOD must be 0 or 1");
        }
        REXCVAR_SET(gta4_force_highest_lod, highest_lod == "1");
        const char* lod_distance_override = std::getenv("THEFT4_LOD_SELECTION_BIAS");
        const std::string_view lod_distance =
            lod_distance_override ? lod_distance_override : "1";
        if (lod_distance != "1" && lod_distance != "1.75") {
            throw std::runtime_error("THEFT4_LOD_SELECTION_BIAS must be 1 or 1.75");
        }
        REXCVAR_SET(gta4_lod_selection_distance_scale,
                    lod_distance == "1.75" ? 1.75 : 1.0);

        const char* reflection_override = std::getenv("THEFT4_REFLECTION_RESOLUTION");
        const std::string_view reflection =
            reflection_override ? reflection_override : "original";
        if (reflection != "original" && reflection != "1080p" && reflection != "full") {
            throw std::runtime_error(
                "THEFT4_REFLECTION_RESOLUTION must be original, 1080p, or full");
        }
        REXCVAR_SET(gta4_reflection_resolution, std::string(reflection));

        const char* anti_aliasing_override = std::getenv("THEFT4_ANTI_ALIASING");
        const std::string_view anti_aliasing =
            anti_aliasing_override ? anti_aliasing_override : "smaa";
        if (anti_aliasing != "off" && anti_aliasing != "fxaa" &&
            anti_aliasing != "smaa") {
            throw std::runtime_error("THEFT4_ANTI_ALIASING must be off, fxaa, or smaa");
        }
        REXCVAR_SET(gta4_native_anti_aliasing, std::string(anti_aliasing));
        REXLOG_INFO(
            "Theft4 graphics: shadows={} ({} map, {}x range) draw-distance={}x "
            "drawable-limit={} highest-lod={} lod-selection-bias={} reflections={} anti-aliasing={}",
            shadow, shadow_map_size, shadow_distance, draw_distance_scale,
            drawable_reference_limit, highest_lod == "1", lod_distance, reflection, anti_aliasing);

        // Use both independently owned native frame slots so the CPU can record
        // frame n+1 while the GPU completes frame n.  The renderer keeps command
        // buffers, upload storage, descriptors, constants, query/readback state
        // and submission fences per slot.  Preserve the launch override so a
        // device run can immediately return to the one-slot stability baseline.
        uint32_t native_frame_slots = 2;
        const char* frames = std::getenv("THEFT4_NATIVE_FRAMES_IN_FLIGHT");
        if (frames) {
            const std::string_view value(frames);
            if (value != "1" && value != "2") {
                throw std::runtime_error(
                    "THEFT4_NATIVE_FRAMES_IN_FLIGHT must be 1 or 2");
            }
            native_frame_slots = value == "1" ? 1u : 2u;
        }
        REXCVAR_SET(gta4_native_frames_in_flight, native_frame_slots);
        REXLOG_INFO("Theft4 native frame-resource slots set to {} ({})",
                    native_frame_slots, frames ? "launch override" : "iOS default");
#ifdef THEFT4_LAB_BUILD
        // A manual performance capture is bounded to a short, explicit run.
        // Spend that diagnostic budget on pass-level GPU timestamps so an
        // exterior capture can distinguish scene draws, reflections and
        // post-processing instead of reporting one unattributed envelope.
        // The profiler emits no timestamps during normal play.
        REXCVAR_SET(gta4_profile_native_detailed_gpu, true);
        REXCVAR_SET(gta4_profile_native_detailed_cpu, true);
        // Keep the all-frame publication/stage trace cheap, and sample enough
        // deep frames to identify CPU operations and GPU pass families without
        // placing hundreds of timestamp boundaries into every submitted frame.
        REXCVAR_SET(gta4_profile_native_gpu_query_budget, 128u);
        REXCVAR_SET(gta4_profile_native_interval, 3u);
        REXCVAR_SET(gta4_profile_native_samples, 120u);
        const char* capture_setting = std::getenv("THEFT4_PERFORMANCE_CAPTURE");
        const bool capture_on_launch = capture_setting &&
            std::string_view(capture_setting) == "1";
        REXCVAR_SET(gta4_profile_native_autostart, capture_on_launch);
        REXLOG_INFO(
            "Theft4 bounded profiler ready: 120 samples / 3-frame interval / "
            "128 GPU boundaries; capture on launch: {}", capture_on_launch);
        // Lab-only default; a fresh launch with 0 restores strict fetch identity
        // in the same executable for controlled A/B runs. Ordinary builds keep
        // the renderer's conservative false default.
        const char* content_cache_override = std::getenv("THEFT4_LAB_TEXTURE_CONTENT_CACHE");
        const std::string_view content_cache =
            content_cache_override ? content_cache_override : "1";
        if (content_cache != "0" && content_cache != "1") {
            throw std::runtime_error("THEFT4_LAB_TEXTURE_CONTENT_CACHE must be 0 or 1");
        }
        REXCVAR_SET(gta4_native_texture_content_cache, content_cache == "1");
        REXLOG_INFO("Theft4 Lab texture content cache: {} ({})",
                    content_cache == "1" ? "enabled" : "strict baseline",
                    content_cache_override ? "launch override" : "Lab default");
        REXCVAR_SET(gta4_native_worker_stall_attribution, true);
        REXLOG_INFO("Theft4 Lab render-worker stall attribution enabled");
        // New-area pipelines may still be compiling when their first draw is
        // recorded. In the Lab, defer that draw instead of blocking the whole
        // render worker. Set 0 to restore the exact synchronous wait path.
        const char* async_pipeline_override =
            std::getenv("THEFT4_LAB_ASYNC_PIPELINES");
        const std::string_view async_pipelines =
            async_pipeline_override ? async_pipeline_override : "1";
        if (async_pipelines != "0" && async_pipelines != "1") {
            throw std::runtime_error("THEFT4_LAB_ASYNC_PIPELINES must be 0 or 1");
        }
        REXCVAR_SET(gta4_native_async_pipeline_no_wait, async_pipelines == "1");
        REXLOG_INFO("Theft4 Lab asynchronous pipelines: {} ({})",
                    async_pipelines == "1" ? "defer pending draws" : "strict wait baseline",
                    async_pipeline_override ? "launch override" : "Lab default");
#endif
#endif
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
        const char* readback_resolve_override =
            std::getenv("THEFT4_READBACK_RESOLVE");
        const std::string_view readback_resolve_mode =
            readback_resolve_override ? readback_resolve_override : "fast";
        if (readback_resolve_mode != "none" &&
            readback_resolve_mode != "some" &&
            readback_resolve_mode != "fast" &&
            readback_resolve_mode != "full") {
            throw std::runtime_error(
                "THEFT4_READBACK_RESOLVE must be none, some, fast, or full");
        }
        REXCVAR_SET(readback_resolve, std::string(readback_resolve_mode));
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
        // Xenia's default closes a Vulkan submission at each guest primary PM4
        // buffer boundary. On MoltenVK that can turn one guest frame into
        // multiple Metal command-buffer/fence cycles. Keep the upstream value
        // by default and expose a controlled comparison without a source fork.
        if (const char* submit_primary = std::getenv("THEFT4_SUBMIT_PRIMARY_END");
            submit_primary) {
            const std::string_view value(submit_primary);
            if (value != "0" && value != "1")
                throw std::runtime_error("THEFT4_SUBMIT_PRIMARY_END must be 0 or 1");
            REXCVAR_SET(vulkan_submit_on_primary_buffer_end, value == "1");
        }
        if (const char* draw_bounds = std::getenv("THEFT4_DRAW_BOUNDS"); draw_bounds) {
            const std::string_view value(draw_bounds);
            if (value != "0" && value != "1")
                throw std::runtime_error("THEFT4_DRAW_BOUNDS must be 0 or 1");
            const bool enabled = value == "1";
            REXCVAR_SET(execute_unclipped_draw_vs_on_cpu, enabled);
            // The broader scissored mode is deliberately excluded from this
            // experiment because it can execute many UI vertices on the CPU.
            REXCVAR_SET(execute_unclipped_draw_vs_on_cpu_with_scissor, false);
        }
        // Keep the mechanism and its diagnostic logging independently
        // controllable so a scored Release run doesn't pay for periodic log
        // formatting. The metrics switch is only for short mechanism checks.
        if (const char* draw_bounds_metrics =
                std::getenv("THEFT4_DRAW_BOUNDS_METRICS");
            draw_bounds_metrics) {
            const std::string_view value(draw_bounds_metrics);
            if (value != "0" && value != "1")
                throw std::runtime_error(
                    "THEFT4_DRAW_BOUNDS_METRICS must be 0 or 1");
            REXCVAR_SET(draw_extent_estimator_diagnostics, value == "1");
        }
        if (const char* transfer_metrics =
                std::getenv("THEFT4_TRANSFER_METRICS");
            transfer_metrics) {
            const std::string_view value(transfer_metrics);
            if (value != "0" && value != "1")
                throw std::runtime_error("THEFT4_TRANSFER_METRICS must be 0 or 1");
            REXCVAR_SET(vulkan_ownership_transfer_diagnostics, value == "1");
        }
        if (const char* transfer_in_draw_pass =
                std::getenv("THEFT4_TRANSFER_IN_DRAW_PASS");
            transfer_in_draw_pass) {
            const std::string_view value(transfer_in_draw_pass);
            if (value != "0" && value != "1")
                throw std::runtime_error(
                    "THEFT4_TRANSFER_IN_DRAW_PASS must be 0 or 1");
            REXCVAR_SET(vulkan_transfer_in_draw_pass, value == "1");
        }
        if (const char* tight_render_area =
                std::getenv("THEFT4_TIGHT_RENDER_AREA");
            tight_render_area) {
            const std::string_view value(tight_render_area);
            if (value != "0" && value != "1")
                throw std::runtime_error(
                    "THEFT4_TIGHT_RENDER_AREA must be 0 or 1");
            REXCVAR_SET(vulkan_tight_render_area, value == "1");
        }
        REXLOG_INFO("Theft4 graphics settings: render_targets={} resolve={} occlusion={} dynamic_rendering={} submit_primary_end={} draw_bounds={} draw_bounds_metrics={} transfer_metrics={} transfer_in_draw_pass={} tight_render_area={}",
                    REXCVAR_GET(render_target_path_vulkan).empty() ? "host" : REXCVAR_GET(render_target_path_vulkan),
                    REXCVAR_GET(readback_resolve), REXCVAR_GET(occlusion_query_enable),
                    REXCVAR_GET(vulkan_dynamic_rendering),
                    REXCVAR_GET(vulkan_submit_on_primary_buffer_end),
                    REXCVAR_GET(execute_unclipped_draw_vs_on_cpu),
                    REXCVAR_GET(draw_extent_estimator_diagnostics),
                    REXCVAR_GET(vulkan_ownership_transfer_diagnostics),
                    REXCVAR_GET(vulkan_transfer_in_draw_pass),
                    REXCVAR_GET(vulkan_tight_render_area));
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
