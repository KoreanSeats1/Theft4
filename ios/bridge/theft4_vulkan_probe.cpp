#include "theft4_vulkan_probe.h"
#include "theft4_rex_vulkan_gate.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include <vulkan/vulkan.h>

#include <rex/logging.h>

namespace {

std::mutex probe_mutex;
VkInstance probe_instance = VK_NULL_HANDLE;
VkPhysicalDevice probe_physical_device = VK_NULL_HANDLE;
VkDevice probe_device = VK_NULL_HANDLE;
uint32_t probe_queue_family = UINT32_MAX;

}  // namespace

bool theft4_vulkan_probe_device() {
  std::lock_guard<std::mutex> lock(probe_mutex);
  if (probe_device != VK_NULL_HANDLE) {
    return true;
  }
  VkApplicationInfo application_info{};
  application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application_info.pApplicationName = "Theft4";
  application_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  application_info.pEngineName = "LibertyRecomp AOT";
  application_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  application_info.apiVersion = VK_API_VERSION_1_1;

  uint32_t extension_count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
  std::vector<VkExtensionProperties> available_extensions(extension_count);
  if (extension_count) {
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count,
                                           available_extensions.data());
  }
  bool has_portability_enumeration = false;
  for (const auto& extension : available_extensions) {
    if (!std::strcmp(extension.extensionName,
                     VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
      has_portability_enumeration = true;
      break;
    }
  }

  const char* extensions[] = {VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.flags = has_portability_enumeration
                          ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
                          : 0;
  create_info.pApplicationInfo = &application_info;
  create_info.enabledExtensionCount = has_portability_enumeration ? 1 : 0;
  create_info.ppEnabledExtensionNames =
      has_portability_enumeration ? extensions : nullptr;

  VkResult result = vkCreateInstance(&create_info, nullptr, &probe_instance);
  if (result != VK_SUCCESS || probe_instance == VK_NULL_HANDLE) {
    REXLOG_ERROR("Theft4 MoltenVK probe: vkCreateInstance failed ({})",
                 static_cast<int32_t>(result));
    return false;
  }

  REXLOG_INFO(
      "Theft4 MoltenVK instance ready: {} extensions, portability enumeration "
      "{}",
      extension_count, has_portability_enumeration ? "enabled" : "unavailable");

  uint32_t device_count = 0;
  result = vkEnumeratePhysicalDevices(probe_instance, &device_count, nullptr);
  if (result != VK_SUCCESS || !device_count) {
    REXLOG_ERROR(
        "Theft4 MoltenVK probe: no Vulkan device (result {}, count {})",
        static_cast<int32_t>(result), device_count);
    vkDestroyInstance(probe_instance, nullptr);
    probe_instance = VK_NULL_HANDLE;
    return false;
  }

  std::vector<VkPhysicalDevice> devices(device_count);
  result = vkEnumeratePhysicalDevices(probe_instance, &device_count, devices.data());
  if (result != VK_SUCCESS || !device_count) {
    vkDestroyInstance(probe_instance, nullptr);
    probe_instance = VK_NULL_HANDLE;
    return false;
  }

  probe_physical_device = devices.front();

  VkPhysicalDeviceProperties properties{};
  VkPhysicalDeviceFeatures features{};
  vkGetPhysicalDeviceProperties(probe_physical_device, &properties);
  vkGetPhysicalDeviceFeatures(probe_physical_device, &features);

  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(probe_physical_device,
                                           &queue_family_count, nullptr);
  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(probe_physical_device,
                                           &queue_family_count,
                                           queue_families.data());
  for (uint32_t i = 0; i < queue_family_count; ++i) {
    if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      probe_queue_family = i;
      break;
    }
  }
  if (probe_queue_family == UINT32_MAX) {
    REXLOG_ERROR("Theft4 MoltenVK probe: no graphics queue family");
    vkDestroyInstance(probe_instance, nullptr);
    probe_instance = VK_NULL_HANDLE;
    probe_physical_device = VK_NULL_HANDLE;
    return false;
  }

  uint32_t device_extension_count = 0;
  vkEnumerateDeviceExtensionProperties(probe_physical_device, nullptr,
                                       &device_extension_count, nullptr);
  std::vector<VkExtensionProperties> device_extensions(device_extension_count);
  if (device_extension_count) {
    vkEnumerateDeviceExtensionProperties(probe_physical_device, nullptr,
                                         &device_extension_count,
                                         device_extensions.data());
  }
  bool has_portability_subset = false;
  for (const auto& extension : device_extensions) {
    if (!std::strcmp(extension.extensionName, "VK_KHR_portability_subset")) {
      has_portability_subset = true;
      break;
    }
  }

  float queue_priority = 1.0f;
  VkDeviceQueueCreateInfo queue_create_info{};
  queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_create_info.queueFamilyIndex = probe_queue_family;
  queue_create_info.queueCount = 1;
  queue_create_info.pQueuePriorities = &queue_priority;

  VkPhysicalDeviceFeatures enabled_features{};
  enabled_features.fragmentStoresAndAtomics = features.fragmentStoresAndAtomics;
  enabled_features.vertexPipelineStoresAndAtomics =
      features.vertexPipelineStoresAndAtomics;
  enabled_features.shaderClipDistance = features.shaderClipDistance;
  enabled_features.shaderCullDistance = features.shaderCullDistance;
  enabled_features.sampleRateShading = features.sampleRateShading;

  const char* portability_subset = "VK_KHR_portability_subset";
  VkDeviceCreateInfo device_create_info{};
  device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_create_info.queueCreateInfoCount = 1;
  device_create_info.pQueueCreateInfos = &queue_create_info;
  device_create_info.enabledExtensionCount = has_portability_subset ? 1 : 0;
  device_create_info.ppEnabledExtensionNames =
      has_portability_subset ? &portability_subset : nullptr;
  device_create_info.pEnabledFeatures = &enabled_features;
  result = vkCreateDevice(probe_physical_device, &device_create_info, nullptr,
                          &probe_device);
  if (result != VK_SUCCESS || probe_device == VK_NULL_HANDLE) {
    REXLOG_ERROR("Theft4 MoltenVK probe: vkCreateDevice failed ({})",
                 static_cast<int32_t>(result));
    vkDestroyInstance(probe_instance, nullptr);
    probe_instance = VK_NULL_HANDLE;
    probe_physical_device = VK_NULL_HANDLE;
    probe_queue_family = UINT32_MAX;
    return false;
  }
  REXLOG_INFO(
      "Theft4 MoltenVK device ready: '{}', Vulkan {}.{}.{}, maxAllocations={}, "
      "fragmentStores={}, vertexStores={}, geometryShader={}",
      properties.deviceName, VK_VERSION_MAJOR(properties.apiVersion),
      VK_VERSION_MINOR(properties.apiVersion),
      VK_VERSION_PATCH(properties.apiVersion),
      properties.limits.maxMemoryAllocationCount,
      features.fragmentStoresAndAtomics != 0,
      features.vertexPipelineStoresAndAtomics != 0,
      features.geometryShader != 0);

  // The raw objects above remain the bootstrap shader-module device until the
  // command processor is switched over to VulkanDevice directly.
  return theft4_rex_vulkan_device_gate();
}

bool theft4_vulkan_validate_spirv(const void* bytes, size_t byte_count,
                                  uint64_t shader_hash, bool is_vertex) {
  if (!bytes || byte_count < sizeof(uint32_t) || (byte_count & 3)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(probe_mutex);
  if (probe_device == VK_NULL_HANDLE) {
    return false;
  }
  VkShaderModuleCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.codeSize = byte_count;
  create_info.pCode = static_cast<const uint32_t*>(bytes);
  VkShaderModule module = VK_NULL_HANDLE;
  const VkResult result =
      vkCreateShaderModule(probe_device, &create_info, nullptr, &module);
  if (result != VK_SUCCESS || module == VK_NULL_HANDLE) {
    REXLOG_ERROR("Theft4 MoltenVK rejected {} SPIR-V {:016X} ({})",
                 is_vertex ? "vertex" : "pixel", shader_hash,
                 static_cast<int32_t>(result));
    return false;
  }
  vkDestroyShaderModule(probe_device, module, nullptr);
  return true;
}

void theft4_vulkan_probe_shutdown() {
  std::lock_guard<std::mutex> lock(probe_mutex);
  if (probe_device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(probe_device);
    vkDestroyDevice(probe_device, nullptr);
    probe_device = VK_NULL_HANDLE;
  }
  if (probe_instance != VK_NULL_HANDLE) {
    vkDestroyInstance(probe_instance, nullptr);
    probe_instance = VK_NULL_HANDLE;
  }
  probe_physical_device = VK_NULL_HANDLE;
  probe_queue_family = UINT32_MAX;
  theft4_rex_vulkan_device_shutdown();
}
