#include <rex/graphics/gta4_native/anti_aliasing_policy.h>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
using namespace rex::graphics::gta4_native;

std::string configured="off",gta4_native_msaa="off";
bool gta4_native_spatial_aa=false,unified=false;
int callback_registrations=0;
namespace rex::cvar {
template<class T>T Query(const char*) {
  if constexpr(std::is_same_v<T,std::string>)return configured;
  else return unified;
}
template<class T>void RegisterChangeCallback(const char*,T){++callback_registrations;}
}
#define REXCVAR_GET(name) name
#define REXLOG_INFO(...) do{}while(0)
std::atomic<AntiAliasingMode> g_renderer_configured_anti_aliasing{AntiAliasingMode::kSmaa};
std::atomic<AntiAliasingMode> g_renderer_active_anti_aliasing{AntiAliasingMode::kSmaa};
std::atomic<bool> g_renderer_anti_aliasing_callback_registered{false};
void OnRendererAntiAliasingChanged(){}
#include "metal-aa-controller.inc"

using X_STATUS=uint32_t;
constexpr X_STATUS X_STATUS_SUCCESS=0,X_STATUS_INVALID_PARAMETER=1,X_STATUS_UNSUCCESSFUL=2;
namespace runtime {
struct FunctionDispatcher {int storage=0;int* memory(){return &storage;}};
}
namespace fixture_system {struct KernelState {};}
struct Backend {
  struct Caps {uint32_t max_image_dimension_2d=16384;} caps;
  bool can_open=true;
  Caps Capabilities()const{return caps;}
};
struct Gta4NativeGraphicsSystem {
  int* memory_=nullptr;
  std::unique_ptr<Backend> frame_backend_=std::make_unique<Backend>();
  std::mutex render_mutex_;std::condition_variable render_condition_;
  bool native_metal_worker_open_complete_=false,native_metal_worker_open_succeeded_=false;
  std::atomic<bool> render_worker_running_{false};
  AntiAliasingMode worker_observed=AntiAliasingMode::kSmaa;
  bool shutdown=false;
  void StartRenderWorker(){
    worker_observed=g_renderer_active_anti_aliasing.load();
    native_metal_worker_open_complete_=true;
    native_metal_worker_open_succeeded_=frame_backend_->can_open;
    render_worker_running_=true;
  }
  void Shutdown(){shutdown=true;render_worker_running_=false;}
  X_STATUS SetupGuestGpu(runtime::FunctionDispatcher*,fixture_system::KernelState*);
};
#define system fixture_system
#include "metal-guest-startup.inc"
#undef system

int main() {
  // No SetupPresentation call: exercise the launch path used by the iPad.
  for(const auto* mode:{"off","fxaa","smaa","msaa2x","ssaa4x"}) {
    configured=mode;unified=true;
    g_renderer_active_anti_aliasing=AntiAliasingMode::kSmaa;
    g_renderer_configured_anti_aliasing=AntiAliasingMode::kSmaa;
    g_renderer_anti_aliasing_callback_registered=false;callback_registrations=0;
    runtime::FunctionDispatcher dispatcher;fixture_system::KernelState kernel;
    Gta4NativeGraphicsSystem graphics;
    assert(graphics.SetupGuestGpu(&dispatcher,&kernel)==X_STATUS_SUCCESS);
    const auto expected=ResolveAntiAliasingConfiguration(configured,gta4_native_msaa,false,unified).mode;
    assert(graphics.worker_observed==expected);
    assert(g_renderer_configured_anti_aliasing.load()==expected);
    assert(callback_registrations==1);
    assert(graphics.SetupGuestGpu(&dispatcher,&kernel)==X_STATUS_SUCCESS);
    assert(callback_registrations==1);
  }
  configured="off";unified=false;
  runtime::FunctionDispatcher dispatcher;fixture_system::KernelState kernel;
  Gta4NativeGraphicsSystem off;
  assert(off.SetupGuestGpu(&dispatcher,&kernel)==X_STATUS_SUCCESS);
  assert(!GetAntiAliasingRoute(off.worker_observed).presentation_smaa);
  assert(!GetAntiAliasingRoute(off.worker_observed).presentation_fxaa);
  Gta4NativeGraphicsSystem invalid;
  assert(invalid.SetupGuestGpu(nullptr,&kernel)==X_STATUS_INVALID_PARAMETER);
  assert(!invalid.render_worker_running_.load());
  Gta4NativeGraphicsSystem unsupported;unsupported.frame_backend_->caps.max_image_dimension_2d=0;
  assert(unsupported.SetupGuestGpu(&dispatcher,&kernel)==X_STATUS_UNSUCCESSFUL);
  assert(!unsupported.render_worker_running_.load());
  Gta4NativeGraphicsSystem failed;failed.frame_backend_->can_open=false;
  assert(failed.SetupGuestGpu(&dispatcher,&kernel)==X_STATUS_UNSUCCESSFUL&&failed.shutdown);
}
