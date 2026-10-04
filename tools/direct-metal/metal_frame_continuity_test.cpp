#include "native_metal_frame_continuity.h"
#include "native_reflection_registry.h"
#include <cassert>
using namespace rex::graphics::gta4_native;
int main() {
  NativeMetalFrameContinuity live;
  NativeReflectionCaptureState reflection;
  auto batch=live;
  batch.postfx.ObserveMarker(RenderPhase::kCompositePostFx,RenderPhaseEvent::kBegin);
  ClaimNativeReflectionCaptureContent(reflection,1,1,batch.frame_identity,0,0,0);
  live=batch; // Accepted internal submission, not a title-frame boundary.
  batch=live;
  assert(batch.postfx.scene_capture_pending());
  assert(HasCurrentNativeReflectionCaptureContent(reflection,batch.frame_identity));
  batch.postfx.FinishSceneCapture(true);
  // Reject speculative work: it must not claim a completed scene capture.
  assert(live.postfx.scene_capture_pending()&&!live.postfx.scene_captured());
  batch=live;batch.postfx.FinishSceneCapture(true);live=batch;
  batch=live;
  assert(!batch.postfx.scene_capture_pending()&&batch.postfx.scene_captured());
  assert(HasCurrentNativeReflectionCaptureContent(reflection,batch.frame_identity));
  live.EndGuestFrame(); // Guest boundary advances even after rejection.
  assert(live.frame_identity==2&&!live.postfx.scene_capture_pending()&&!live.postfx.scene_captured());
  assert(!HasCurrentNativeReflectionCaptureContent(reflection,live.frame_identity));
  batch=live;batch.postfx.ObserveMarker(RenderPhase::kCompositePostFx,RenderPhaseEvent::kBegin);
  live=batch;live.EndGuestFrame();
  assert(live.frame_identity==3&&!live.postfx.scene_capture_pending());
}
