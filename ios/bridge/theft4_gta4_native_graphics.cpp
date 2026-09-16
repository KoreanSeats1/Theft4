#include "theft4_gta4_native_graphics.h"

#include "theft4_metal_presenter.h"

#include <dispatch/dispatch.h>
#include <pthread.h>

#include <memory>
#include <string>

#include <rex/cvar.h>
#include <rex/graphics/gta4_native/anti_aliasing_policy.h>
#include <rex/logging.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/surface.h>
#include <rex/ui/vulkan/presenter.h>
#include <rex/ui/vulkan/provider.h>

#include "../../glue/rexglue-sdk-main/src/graphics/gta4_native/graphics_system.h"

// The desktop frontend normally owns these user-facing GTA IV settings. The
// iOS shell has no desktop GTA4App, so publish the same renderer defaults here
// and pace the guest at the project's 30 FPS target.
REXCVAR_DEFINE_STRING(gta4_aspect_ratio, "16:9", "GTA IV/Graphics/Display",
                      "Render aspect ratio")
    .allowed({"auto", "original", "16:9", "16:10", "3:2", "4:3", "5:4", "21:9", "43:18", "32:9", "32:10"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(gta4_present_mode, "vsync", "GTA IV/Graphics/Display",
                      "Presentation mode")
    .allowed({"auto", "vsync", "mailbox", "immediate"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_UINT32(gta4_frame_limit, 30, "GTA IV/Graphics/Display",
                      "Maximum guest frame rate")
    .allowed({"0", "30", "60", "120"});
REXCVAR_DEFINE_STRING(gta4_native_hdr_mode, "off", "GTA IV/Graphics/HDR", "HDR output mode")
    .allowed({"off", "scrgb", "auto_hdr"});
REXCVAR_DEFINE_BOOL(gta4_native_hdr_mode_unified, false,
                    "GTA IV/Graphics/HDR/Compatibility", "HDR compatibility state");
REXCVAR_DEFINE_DOUBLE(gta4_native_hdr_paper_white_nits, 203.0, "GTA IV/Graphics/HDR",
                      "SDR reference white brightness")
    .range(80.0, 500.0);
REXCVAR_DEFINE_DOUBLE(gta4_native_hdr_peak_nits, 400.0, "GTA IV/Graphics/HDR",
                      "Auto HDR highlight target")
    .range(80.0, 2000.0);
REXCVAR_DEFINE_DOUBLE(gta4_native_auto_hdr_shoulder_start, 0.0,
                      "GTA IV/Graphics/HDR/Advanced", "Auto HDR shoulder start")
    .range(0.0, 1.0);
REXCVAR_DEFINE_DOUBLE(gta4_native_auto_hdr_shoulder_power, 2.5,
                      "GTA IV/Graphics/HDR/Advanced", "Auto HDR shoulder exponent")
    .range(1.0, 10.0);
REXCVAR_DEFINE_UINT32(gta4_shadow_map_base_size, 256, "GTA IV/Graphics/Shadows",
                      "Base shadow-map size")
    .range(256, 1024)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_DOUBLE(gta4_shadow_distance_scale, 1.0, "GTA IV/Graphics/Shadows",
                      "Directional shadow range multiplier")
    .range(1.0, 4.0)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(gta4_reflection_resolution, "original", "GTA IV/Graphics/Reflections",
                      "Reflection resolution")
    .allowed({"original", "1080p", "full"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(gta4_reflection_resolution_cap, "1440p", "GTA IV/Graphics/Reflections",
                      "Maximum reflection resolution")
    .allowed({"1080p", "1440p", "display"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(gta4_mirror_reflection_resolution, "inherit",
                      "GTA IV/Graphics/Reflections/Advanced", "Mirror reflection resolution")
    .allowed({"inherit", "original", "1080p", "full"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(gta4_water_reflection_resolution, "inherit",
                      "GTA IV/Graphics/Reflections/Advanced", "Water reflection resolution")
    .allowed({"inherit", "original", "1080p", "full"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(gta4_environment_reflection_resolution, "inherit",
                      "GTA IV/Graphics/Reflections/Advanced", "Environment reflection resolution")
    .allowed({"inherit", "original", "1080p", "full"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(gta4_reflection_aa, "original", "GTA IV/Graphics/Reflections",
                      "Reflection anti-aliasing")
    .allowed({"original", "off", "2x", "4x"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(gta4_reflection_capture_distance, "original",
                      "GTA IV/Graphics/Reflections/Advanced", "Reflection capture distance")
    .allowed({"original", "extended", "far"});
REXCVAR_DEFINE_STRING(gta4_native_anti_aliasing, "smaa",
                      "GTA IV/Graphics/Anti-Aliasing", "Anti-aliasing mode")
    .allowed({"off", "fxaa", "smaa", "msaa2x", "msaa4x", "ssaa2x", "ssaa4x",
              "ssaa6x", "ssaa8x", "ssaa10x", "ssaa12x", "ssaa14x", "ssaa16x",
              "spatial"});
REXCVAR_DEFINE_BOOL(gta4_native_anti_aliasing_unified, false,
                    "GTA IV/Graphics/Anti-Aliasing/Compatibility",
                    "Unified anti-aliasing compatibility state");
REXCVAR_DEFINE_STRING(gta4_native_smaa_quality, "high",
                      "GTA IV/Graphics/Anti-Aliasing", "SMAA quality")
    .allowed({"low", "medium", "high", "ultra"});
REXCVAR_DEFINE_STRING(gta4_native_upscaler, "native", "GTA IV/Graphics/Upscaling",
                      "Output upscaler")
    .allowed({"native", "fsr1"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(gta4_fsr1_quality, "quality", "GTA IV/Graphics/Upscaling",
                      "FSR 1 quality")
    .allowed({"ultra_quality", "quality", "balanced", "performance"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_DOUBLE(gta4_fsr1_sharpness_reduction, 0.2, "GTA IV/Graphics/Upscaling",
                      "FSR 1 sharpness reduction")
    .range(0.0, 2.0)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(gta4_force_highest_lod, false, "GTA IV/Graphics/LOD",
                    "Prefer the highest resident model LOD");
REXCVAR_DEFINE_DOUBLE(gta4_draw_distance_scale, 1.0, "GTA IV/Graphics/LOD",
                      "World-distance multiplier")
    .range(1.0, 4.0);
REXCVAR_DEFINE_UINT32(gta4_drawable_reference_limit, 13000, "GTA IV/Graphics/LOD",
                      "Drawable-reference capacity")
    .range(13000, 40000)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

// GTA4App owns the mutable anti-aliasing controller on desktop. The iOS shell
// currently exposes a launch-time setting only, so resolving the configured
// mode directly is equivalent and keeps the renderer frontend-independent.
namespace rex::graphics::gta4_native {

AntiAliasingMode GetActiveAntiAliasingMode() {
  const auto configured = ParseAntiAliasingMode(
      REXCVAR_GET(gta4_native_anti_aliasing));
  return configured.value_or(AntiAliasingMode::kOff);
}

}  // namespace rex::graphics::gta4_native

namespace {

class Theft4NativeMetalLayerSurface final : public rex::ui::Surface {
 public:
  explicit Theft4NativeMetalLayerSurface(void* layer) : layer_(layer) {}

  TypeIndex GetType() const override { return kTypeIndex_CAMetalLayer; }
  void* GetNativePresentationHandle() const override { return layer_; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override {
    return theft4_metal_bound_layer_size(&width_out, &height_out);
  }

 private:
  void* layer_ = nullptr;
};

struct AttachContext {
  rex::ui::vulkan::VulkanPresenter* presenter;
  rex::ui::Surface* surface;
};

void AttachNativePresenter(void* opaque) {
  auto* context = static_cast<AttachContext*>(opaque);
  context->presenter->SetWindowSurfaceFromUIThread(nullptr, context->surface);
}

}  // namespace

std::unique_ptr<rex::system::IGraphicsSystem>
theft4_create_gta4_native_graphics() {
  void* layer = theft4_metal_bound_layer();
  if (!layer) {
    REXLOG_ERROR("Theft4 gta4-native renderer has no bound CAMetalLayer");
    return nullptr;
  }

  // High-resolution replacement atlases aren't packaged in the iOS app yet.
  // Use the title's stock fonts rather than performing failed filesystem
  // probes or silently losing text. This preserves retail visual fidelity.
  rex::cvar::SetFlagByName("gta4_native_vector_fonts", "false");

  auto provider = rex::ui::vulkan::VulkanProvider::Create(
      false, true, true, true);
  if (!provider) {
    REXLOG_ERROR("Theft4 gta4-native Vulkan provider rejected the Apple GPU");
    return nullptr;
  }
  auto presenter = provider->CreatePresenter([](bool responsible, bool) {
    REXLOG_ERROR("Theft4 gta4-native presenter reported GPU loss ({})",
                 responsible ? "responsible" : "external");
  });
  if (!presenter) {
    REXLOG_ERROR("Theft4 gta4-native presenter creation failed");
    return nullptr;
  }

  auto surface = std::make_unique<Theft4NativeMetalLayerSurface>(layer);
  AttachContext context{
      static_cast<rex::ui::vulkan::VulkanPresenter*>(presenter.get()),
      surface.get()};
  if (pthread_main_np()) {
    AttachNativePresenter(&context);
  } else {
    dispatch_sync_f(dispatch_get_main_queue(), &context, AttachNativePresenter);
  }

  REXLOG_INFO("Theft4 selected gta4-native on '{}'",
              provider->vulkan_device()->properties().deviceName);
  return std::make_unique<
      rex::graphics::gta4_native::Gta4NativeGraphicsSystem>(
      std::move(provider), std::move(presenter), std::move(surface));
}
