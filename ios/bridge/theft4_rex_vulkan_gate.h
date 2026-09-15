#pragma once

namespace rex::ui::vulkan {
class VulkanDevice;
class VulkanPresenter;
}

// Exercises LibertyRecomp's production Vulkan instance and device selection
// against the statically linked MoltenVK implementation.
bool theft4_rex_vulkan_device_gate();

// Non-owning. Valid after a successful gate until shutdown. The embedded
// command processor uses the same production device rather than creating a
// second renderer-side logical device.
rex::ui::vulkan::VulkanDevice* theft4_rex_vulkan_device();
rex::ui::vulkan::VulkanPresenter* theft4_rex_vulkan_presenter();
void theft4_rex_vulkan_device_shutdown();
