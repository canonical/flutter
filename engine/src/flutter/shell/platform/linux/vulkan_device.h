// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_VULKAN_DEVICE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_VULKAN_DEVICE_H_

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/fml/unique_fd.h"
#include "flutter/shell/platform/linux/gpu_device.h"

namespace flutter {

class VulkanDevice;

// A Vulkan image sharing the memory of a dmabuf.
class VulkanImage {
 public:
  ~VulkanImage();

  VkImage image() const { return image_; }

 private:
  friend class VulkanDevice;

  explicit VulkanImage(VulkanDevice* device) : device_(device) {}

  VulkanDevice* device_;
  VkImage image_ = VK_NULL_HANDLE;
  VkDeviceMemory memory_ = VK_NULL_HANDLE;

  FML_DISALLOW_COPY_AND_ASSIGN(VulkanImage);
};

// The Vulkan device Flutter renders with when using the Vulkan renderer,
// shared by all engines.
//
// Flutter renders into dmabufs that are also imported into OpenGL, which is
// used to present them. Vulkan and OpenGL are synchronized with sync files.
//
// The engine uses the device's queue from multiple threads, so access to it
// is serialized by wrapping the queue functions it is given.
//
// This may be used from any thread.
class VulkanDevice {
 public:
  // Creates a Vulkan device on the same GPU as |gpu|. Returns nullptr if
  // Vulkan is not available or doesn't support what Flutter needs.
  static std::unique_ptr<VulkanDevice> Create(GpuDevice* gpu);

  ~VulkanDevice();

  VkInstance instance() const { return instance_; }
  VkPhysicalDevice physical_device() const { return physical_device_; }
  VkDevice device() const { return device_; }
  uint32_t queue_family_index() const { return queue_family_index_; }
  VkQueue queue() const { return queue_; }
  uint32_t api_version() const { return api_version_; }

  // The extensions enabled on the instance and device.
  const std::vector<const char*>& instance_extensions() const {
    return instance_extensions_;
  }
  const std::vector<const char*>& device_extensions() const {
    return device_extensions_;
  }

  // The device name, e.g. "AMD Radeon Graphics (RADV RENOIR)".
  const std::string& name() const { return name_; }

  // Resolves Vulkan functions for the engine. Queue functions are replaced
  // with ones that serialize access to the queue.
  static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
  GetInstanceProcAddress(VkInstance instance, const char* name);

  // The format all images use, matching DRM_FORMAT_ARGB8888.
  static constexpr VkFormat kFormat = VK_FORMAT_B8G8R8A8_UNORM;

  // The DRM format modifiers dmabufs can have to be rendered into with Vulkan.
  const std::vector<uint64_t>& modifiers() const { return modifiers_; }

  // Imports a dmabuf of DRM_FORMAT_ARGB8888 with an explicit |modifier|.
  // Returns nullptr on failure.
  std::unique_ptr<VulkanImage> ImportDmabuf(
      int width,
      int height,
      uint64_t modifier,
      const GpuDevice::DmabufPlane* planes,
      size_t n_planes);

  // Hands |images| to other APIs once all the rendering submitted so far has
  // completed. Returns a sync file that signals when that happens, or an
  // invalid fd if rendering has already been waited for.
  fml::UniqueFD ReleaseImages(const std::vector<VkImage>& images);

  // Waits for all submitted work to complete.
  void WaitIdle();

 private:
  VulkanDevice() = default;

  bool Initialize(GpuDevice* gpu);
  bool CreateInstance();
  bool PickPhysicalDevice(GpuDevice* gpu);
  bool CreateDevice();
  bool LoadDeviceFunctions();
  void FindModifiers();

  // Wrappers for the engine that lock |queue_mutex_|.
  static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
  GetDeviceProcAddrLocked(VkDevice device, const char* name);
  static VKAPI_ATTR VkResult VKAPI_CALL
  QueueSubmitLocked(VkQueue queue,
                    uint32_t count,
                    const VkSubmitInfo* submits,
                    VkFence fence);
  static VKAPI_ATTR VkResult VKAPI_CALL QueueWaitIdleLocked(VkQueue queue);
  static VKAPI_ATTR VkResult VKAPI_CALL DeviceWaitIdleLocked(VkDevice device);

  void* library_ = nullptr;
  uint32_t api_version_ = VK_API_VERSION_1_1;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  uint32_t queue_family_index_ = 0;
  VkQueue queue_ = VK_NULL_HANDLE;
  std::string name_;
  std::vector<const char*> instance_extensions_;
  std::vector<const char*> device_extensions_;
  std::vector<uint64_t> modifiers_;

  std::mutex queue_mutex_;

  // Used to release images, guarded by |queue_mutex_|.
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  struct Submission {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore semaphore = VK_NULL_HANDLE;
  };
  std::vector<Submission> submissions_;
  size_t next_submission_ = 0;

  // Vulkan functions.
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
  PFN_vkDestroyInstance vkDestroyInstance = nullptr;
  PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
  PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 = nullptr;
  PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2 = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties
      vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
  PFN_vkGetPhysicalDeviceFormatProperties2
      vkGetPhysicalDeviceFormatProperties2 = nullptr;
  PFN_vkGetPhysicalDeviceImageFormatProperties2
      vkGetPhysicalDeviceImageFormatProperties2 = nullptr;
  PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties =
      nullptr;
  PFN_vkEnumerateDeviceExtensionProperties
      vkEnumerateDeviceExtensionProperties = nullptr;
  PFN_vkCreateDevice vkCreateDevice = nullptr;
  PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
  PFN_vkDestroyDevice vkDestroyDevice = nullptr;
  PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
  PFN_vkQueueSubmit vkQueueSubmit = nullptr;
  PFN_vkQueueWaitIdle vkQueueWaitIdle = nullptr;
  PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
  PFN_vkCreateImage vkCreateImage = nullptr;
  PFN_vkDestroyImage vkDestroyImage = nullptr;
  PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements = nullptr;
  PFN_vkAllocateMemory vkAllocateMemory = nullptr;
  PFN_vkFreeMemory vkFreeMemory = nullptr;
  PFN_vkBindImageMemory vkBindImageMemory = nullptr;
  PFN_vkGetMemoryFdPropertiesKHR vkGetMemoryFdPropertiesKHR = nullptr;
  PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
  PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
  PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
  PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
  PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
  PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;
  PFN_vkCreateFence vkCreateFence = nullptr;
  PFN_vkDestroyFence vkDestroyFence = nullptr;
  PFN_vkWaitForFences vkWaitForFences = nullptr;
  PFN_vkResetFences vkResetFences = nullptr;
  PFN_vkCreateSemaphore vkCreateSemaphore = nullptr;
  PFN_vkDestroySemaphore vkDestroySemaphore = nullptr;
  PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR = nullptr;

  friend class VulkanImage;

  FML_DISALLOW_COPY_AND_ASSIGN(VulkanDevice);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_VULKAN_DEVICE_H_
