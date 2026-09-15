#pragma once

#include <cstddef>
#include <cstdint>

// Bootstrap for the Vulkan-over-MoltenVK renderer path. The instance and
// logical device stay alive so real translated Xenos SPIR-V can be accepted by
// MoltenVK before the full render-target and draw backend is connected.
bool theft4_vulkan_probe_device();
bool theft4_vulkan_validate_spirv(const void* bytes, size_t byte_count,
                                  uint64_t shader_hash, bool is_vertex);
void theft4_vulkan_probe_shutdown();
