#pragma once

#include <memory>

namespace rex::system {
class IGraphicsSystem;
}

// Creates LibertyRecomp's GTA-IV-specific Vulkan renderer on the UIKit-owned
// CAMetalLayer. Returns null if the Apple GPU lacks a required native feature.
std::unique_ptr<rex::system::IGraphicsSystem>
theft4_create_gta4_native_graphics();
#ifdef THEFT4_DIRECT_METAL_BACKEND
// Uses the same title command frontend with a direct Metal worker. No Vulkan
// provider, presenter, device or surface is constructed on this route.
std::unique_ptr<rex::system::IGraphicsSystem>
theft4_create_gta4_metal_graphics();
#endif
