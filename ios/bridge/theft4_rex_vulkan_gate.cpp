#include "theft4_rex_vulkan_gate.h"
#include "theft4_metal_presenter.h"

#include <dispatch/dispatch.h>
#include <pthread.h>
#include <memory>
#include <mutex>
#include <vector>

#include <rex/logging.h>
#include <rex/ui/surface.h>
#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/instance.h>
#include <rex/ui/vulkan/presenter.h>
#include <rex/ui/vulkan/ui_samplers.h>

namespace {

std::mutex production_device_mutex;
std::unique_ptr<rex::ui::vulkan::VulkanInstance> production_instance;
std::unique_ptr<rex::ui::vulkan::VulkanDevice> production_device;
std::unique_ptr<rex::ui::vulkan::UISamplers> production_ui_samplers;
std::unique_ptr<rex::ui::vulkan::VulkanPresenter> production_presenter;

class Theft4MetalLayerSurface final : public rex::ui::Surface {
 public:
  explicit Theft4MetalLayerSurface(void* layer) : layer_(layer) {}

  TypeIndex GetType() const override { return kTypeIndex_CAMetalLayer; }
  void* GetNativePresentationHandle() const override { return layer_; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override {
    return theft4_metal_bound_layer_size(&width_out, &height_out);
  }

 private:
  void* layer_ = nullptr;
};

std::unique_ptr<Theft4MetalLayerSurface> production_surface;

}  // namespace

bool theft4_rex_vulkan_device_gate() {
  std::lock_guard<std::mutex> lock(production_device_mutex);
  if (production_device) {
    return true;
  }
  production_instance =
      rex::ui::vulkan::VulkanInstance::Create(true, false);
  if (!production_instance) {
    REXLOG_ERROR("Theft4 embedded Liberty VulkanInstance creation failed");
    return false;
  }

  std::vector<VkPhysicalDevice> physical_devices;
  production_instance->EnumeratePhysicalDevices(physical_devices);
  if (physical_devices.empty()) {
    REXLOG_ERROR("Theft4 embedded Liberty VulkanInstance found no device");
    production_instance.reset();
    return false;
  }

  production_device =
      rex::ui::vulkan::VulkanDevice::CreateIfSupported(
          production_instance.get(), physical_devices.front(), true, true);
  if (!production_device) {
    REXLOG_ERROR(
        "Theft4 embedded Liberty VulkanDevice rejected the Apple GPU");
    production_instance.reset();
    return false;
  }
  production_ui_samplers =
      rex::ui::vulkan::UISamplers::Create(production_device.get());
  if (!production_ui_samplers) {
    REXLOG_ERROR("Theft4 embedded Vulkan UI sampler creation failed");
    production_device.reset();
    production_instance.reset();
    return false;
  }
  production_presenter = rex::ui::vulkan::VulkanPresenter::Create(
      [](bool responsible, bool) {
        REXLOG_ERROR("Theft4 embedded Vulkan presenter reported GPU loss ({})",
                     responsible ? "responsible" : "external");
      },
      production_device.get(), production_ui_samplers.get());
  if (!production_presenter) {
    REXLOG_ERROR("Theft4 embedded Vulkan presenter creation failed");
    production_ui_samplers.reset();
    production_device.reset();
    production_instance.reset();
    return false;
  }
  void* layer = theft4_metal_bound_layer();
  if (!layer) {
    REXLOG_ERROR("Theft4 embedded Vulkan presenter has no bound CAMetalLayer");
    production_presenter.reset();
    production_ui_samplers.reset();
    production_device.reset();
    production_instance.reset();
    return false;
  }
  production_surface = std::make_unique<Theft4MetalLayerSurface>(layer);
  auto attach_surface = [](void*) {
    production_presenter->SetWindowSurfaceFromUIThread(
        nullptr, production_surface.get());
  };
  if (pthread_main_np()) {
    attach_surface(nullptr);
  } else {
    dispatch_sync_f(dispatch_get_main_queue(), nullptr, attach_surface);
  }
  REXLOG_INFO("Theft4 production Liberty Vulkan device gate passed: '{}'",
              production_device->properties().deviceName);
  REXLOG_INFO("Theft4 production Vulkan presenter attached to UIKit CAMetalLayer");
  return true;
}

rex::ui::vulkan::VulkanDevice* theft4_rex_vulkan_device() {
  std::lock_guard<std::mutex> lock(production_device_mutex);
  return production_device.get();
}

rex::ui::vulkan::VulkanPresenter* theft4_rex_vulkan_presenter() {
  std::lock_guard<std::mutex> lock(production_device_mutex);
  return production_presenter.get();
}

void theft4_rex_vulkan_device_shutdown() {
  std::lock_guard<std::mutex> lock(production_device_mutex);
  if (production_presenter) {
    auto detach_surface = [](void*) {
      production_presenter->SetWindowSurfaceFromUIThread(nullptr, nullptr);
    };
    if (pthread_main_np()) {
      detach_surface(nullptr);
    } else {
      dispatch_sync_f(dispatch_get_main_queue(), nullptr, detach_surface);
    }
  }
  production_presenter.reset();
  production_surface.reset();
  production_ui_samplers.reset();
  production_device.reset();
  production_instance.reset();
}
