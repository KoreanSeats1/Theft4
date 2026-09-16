// Private GPU-frame experiment. No guest executable is loaded or executed.
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string_view>
#include <thread>
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/emulator.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/metal/metal_graphics_system.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xenia/gpu/trace_player.h"
#include "xenia/ui/ios/app/windowed_app_context_ios.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/window.h"

DECLARE_path(log_file);
DECLARE_int32(log_level);
// The standalone app supplies flags normally defined by XeniOS's app entry point.
DEFINE_string(apu, "nop", "No audio for captured GPU-frame replay.", "APU");
DEFINE_string(gpu, "metal", "Native Metal GPU-frame replay.", "GPU");

@interface ReplayView : UIView
@end
@implementation ReplayView
+ (Class)layerClass { return [CAMetalLayer class]; }
- (CGFloat)xeniaDrawableAspectRatio { return 16.0 / 9.0; }
@end

@interface ReplayDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation ReplayDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options {
  application.idleTimerDisabled = YES;
  return YES;
}
@end

@interface ReplaySceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation ReplaySceneDelegate {
  std::unique_ptr<xe::ui::IOSWindowedAppContext> context_;
  std::unique_ptr<xe::ui::Window> render_window_;
  std::unique_ptr<xe::Emulator> runtime_;
  std::unique_ptr<xe::gpu::TracePlayer> player_;
}
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options {
  const auto documents = std::filesystem::path(
      NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0].UTF8String);
  cvars::log_file = documents / "replay.log";
  cvars::log_level = 2;
  xe::InitializeLogging("Theft4MetalReplay");
  // require_cpu_backend=false alone still chooses A64 for cpu=any.
  cvars::cpu = "null";
  self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene*)scene];
  UIViewController* controller = [UIViewController new];
  controller.view = [[ReplayView alloc] initWithFrame:self.window.bounds];
  controller.view.backgroundColor = UIColor.blackColor;
  self.window.rootViewController = controller;
  [self.window makeKeyAndVisible];
  [controller.view layoutIfNeeded];
  context_ = std::make_unique<xe::ui::IOSWindowedAppContext>();
  context_->set_metal_view(controller.view);
  context_->set_view_controller(controller);
  render_window_ = xe::ui::Window::Create(*context_, "Theft4 Metal Replay", 1280, 720);
  if (!render_window_->Open()) return;
  // Keep UIApplication responsive while replay restores guest memory and compiles shaders.
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    [self replayAt:documents];
  });
}
- (void)replayAt:(const std::filesystem::path&)documents {
  const auto trace = documents / "scene.xtr";
  if (!std::filesystem::exists(trace)) {
    XELOGE("Replay: import Documents/scene.xtr and relaunch");
    return;
  }
  for (const auto* name : {"storage", "content", "cache"})
    std::filesystem::create_directories(documents / name);
  runtime_ = std::make_unique<xe::Emulator>("", documents / "storage",
                                          documents / "content", documents / "cache");
  const char* requested_gpu = std::getenv("THEFT4_REPLAY_GPU");
  const bool use_vulkan = requested_gpu && std::string_view(requested_gpu) == "vulkan";
  cvars::gpu = use_vulkan ? "vulkan" : "metal";
  // Trace playback executes each captured draw only once. With asynchronous
  // compilation, the live emulator's placeholder behavior skips draws whose
  // pipelines aren't ready and never replays them, invalidating the image.
  cvars::async_shader_compilation = false;
  const std::string backend_name = use_vulkan ? "vulkan" : "metal";
  XELOGI("Replay: initializing null-CPU runtime with {} GPU; shader compilation synchronous",
         backend_name);
  auto status = runtime_->Setup(render_window_.get(), nullptr, false, nullptr,
      [use_vulkan]() -> std::unique_ptr<xe::gpu::GraphicsSystem> {
        if (use_vulkan) {
          return std::make_unique<xe::gpu::vulkan::VulkanGraphicsSystem>();
        }
        return std::make_unique<xe::gpu::metal::MetalGraphicsSystem>();
      }, nullptr);
  if (status || (status = runtime_->SetupSubsystems())) {
    XELOGE("Replay: setup failed {:08X}", status);
    return;
  }
  auto* graphics = runtime_->graphics_system();
  dispatch_sync(dispatch_get_main_queue(), ^{
    render_window_->SetPresenter(graphics->presenter());
  });
  player_ = std::make_unique<xe::gpu::TracePlayer>(graphics);
  if (!player_->Open(trace.string()) || !player_->frame_count() ||
      player_->frame(0)->commands.empty()) {
    XELOGE("Replay: trace has no complete commands");
    return;
  }
  XELOGI("Replay: loaded {} frames, {} commands in first frame; CPU backend=null",
         player_->frame_count(), player_->frame(0)->commands.size());
  const auto start = std::chrono::steady_clock::now();
  player_->SeekCommand(int(player_->frame(0)->commands.size()) - 1);
  // TracePlayer's break-on-swap path returns without signaling playback_event_.
  // A callback queued after playback fences CPU command processing instead.
  // CaptureGuestOutput below performs the separate GPU/readback synchronization.
  auto completion = std::make_shared<std::promise<void>>();
  auto finished = completion->get_future();
  graphics->command_processor()->CallInThread([completion] { completion->set_value(); });
  if (finished.wait_for(std::chrono::seconds(60)) != std::future_status::ready) {
    XELOGE("Replay: command playback did not complete within 60 seconds");
    return;
  }
  xe::ui::RawImage image;
  if (!graphics->presenter()->CaptureGuestOutput(image)) {
    XELOGE("Replay: GPU commands finished but output capture failed");
    return;
  }
  std::ofstream output(documents / (backend_name + "-output.ppm"), std::ios::binary);
  output << "P6\n" << image.width << " " << image.height << "\n255\n";
  for (uint32_t y = 0; y < image.height; ++y)
    for (uint32_t x = 0; x < image.width; ++x)
      output.write(reinterpret_cast<const char*>(image.data.data() + y * image.stride + x * 4), 3);
  XELOGI("Replay: output {}x{}, cold replay/capture {} ms (not an FPS benchmark)",
         image.width, image.height,
         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}
@end

int main(int argc, char** argv) {
  @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(ReplayDelegate.class)); }
}
