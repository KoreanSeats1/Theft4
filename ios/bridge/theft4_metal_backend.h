#pragma once
#include "theft4_frame_backend.h"
namespace theft4::metal {
// layer is a retained CAMetalLayer inside the implementation; nullptr creates
// a headless worker for validation. GPU objects open and close on that worker.
std::unique_ptr<render::FrameBackend> CreateFrameBackend(
    void* layer,std::string libraries,uint32_t maximum_frames=2,
    void (*diagnostic)(const char*)=nullptr);
}
