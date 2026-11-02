// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/vulkan_device.h"

#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <cstring>

#include "flutter/fml/logging.h"

namespace flutter {

namespace {

// The version Impeller requires.
constexpr uint32_t kApiVersion = VK_API_VERSION_1_1;

// How long to wait for a previous release to complete before reusing its
// command buffer.
constexpr uint64_t kFenceTimeoutNs = 1000000000;

// The number of releases that can be in flight.
constexpr size_t kMaxSubmissions = 4;

// What images are used for. Impeller renders into them and may copy from
// them.
constexpr VkImageUsageFlags kImageUsage =
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

constexpr VkFormatFeatureFlags kFormatFeatures =
    VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
    VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
    VK_FORMAT_FEATURE_TRANSFER_DST_BIT;

// Device extensions that must be supported.
constexpr const char* kRequiredDeviceExtensions[] = {
    // Required by Impeller, though no swapchain is created.
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
    VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
    VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
};

// Window system extensions, one of which Impeller requires to be enabled
// even though Flutter doesn't create Vulkan surfaces.
constexpr const char* kSurfaceExtensions[] = {
    "VK_KHR_wayland_surface",
    "VK_KHR_xcb_surface",
    "VK_KHR_xlib_surface",
};

// The device whose queue the engine's queue functions lock. All engines share
// one device.
VulkanDevice* g_device = nullptr;

bool HasExtension(const std::vector<VkExtensionProperties>& extensions,
                  const char* name) {
  for (const VkExtensionProperties& extension : extensions) {
    if (strcmp(extension.extensionName, name) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

VulkanImage::~VulkanImage() {
  if (image_ != VK_NULL_HANDLE) {
    device_->vkDestroyImage(device_->device_, image_, nullptr);
  }
  if (memory_ != VK_NULL_HANDLE) {
    device_->vkFreeMemory(device_->device_, memory_, nullptr);
  }
}

std::unique_ptr<VulkanDevice> VulkanDevice::Create(GpuDevice* gpu) {
  if (g_device != nullptr) {
    FML_LOG(ERROR) << "Only one Vulkan device can be created";
    return nullptr;
  }
  std::unique_ptr<VulkanDevice> device(new VulkanDevice());
  if (!device->Initialize(gpu)) {
    return nullptr;
  }
  g_device = device.get();
  return device;
}

VulkanDevice::~VulkanDevice() {
  if (device_ != VK_NULL_HANDLE && vkDestroyDevice != nullptr) {
    if (command_pool_ != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(device_);
      for (const Submission& submission : submissions_) {
        vkDestroyFence(device_, submission.fence, nullptr);
        vkDestroySemaphore(device_, submission.semaphore, nullptr);
      }
      vkDestroyCommandPool(device_, command_pool_, nullptr);
    }
    vkDestroyDevice(device_, nullptr);
  }
  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
  }
  if (library_ != nullptr) {
    dlclose(library_);
  }
  if (g_device == this) {
    g_device = nullptr;
  }
}

bool VulkanDevice::Initialize(GpuDevice* gpu) {
  library_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  if (library_ == nullptr) {
    FML_LOG(ERROR) << "Vulkan is not available: " << dlerror();
    return false;
  }
  vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
      dlsym(library_, "vkGetInstanceProcAddr"));
  if (vkGetInstanceProcAddr == nullptr) {
    FML_LOG(ERROR) << "Vulkan loader is missing vkGetInstanceProcAddr";
    return false;
  }

  if (!CreateInstance() || !PickPhysicalDevice(gpu) || !CreateDevice() ||
      !LoadDeviceFunctions()) {
    return false;
  }

  FindModifiers();
  if (modifiers_.empty()) {
    FML_LOG(ERROR) << "Vulkan can't render into dmabufs";
    return false;
  }

  VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = queue_family_index_,
  };
  if (vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_) !=
      VK_SUCCESS) {
    FML_LOG(ERROR) << "Failed to create Vulkan command pool";
    return false;
  }
  submissions_.resize(kMaxSubmissions);
  for (Submission& submission : submissions_) {
    VkCommandBufferAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    VkExportSemaphoreCreateInfo export_info = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    };
    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &export_info,
    };
    if (vkAllocateCommandBuffers(device_, &allocate_info,
                                 &submission.command_buffer) != VK_SUCCESS ||
        vkCreateFence(device_, &fence_info, nullptr, &submission.fence) !=
            VK_SUCCESS ||
        vkCreateSemaphore(device_, &semaphore_info, nullptr,
                          &submission.semaphore) != VK_SUCCESS) {
      FML_LOG(ERROR) << "Failed to create Vulkan synchronization objects";
      return false;
    }
  }

  return true;
}

bool VulkanDevice::CreateInstance() {
  auto vkEnumerateInstanceVersion =
      reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
          vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
  uint32_t version = VK_API_VERSION_1_0;
  if (vkEnumerateInstanceVersion != nullptr) {
    vkEnumerateInstanceVersion(&version);
  }
  if (version < kApiVersion) {
    FML_LOG(ERROR) << "Vulkan 1.1 is not available";
    return false;
  }

  auto vkEnumerateInstanceExtensionProperties =
      reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
          vkGetInstanceProcAddr(nullptr,
                                "vkEnumerateInstanceExtensionProperties"));
  auto vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
      vkGetInstanceProcAddr(nullptr, "vkCreateInstance"));
  if (vkEnumerateInstanceExtensionProperties == nullptr ||
      vkCreateInstance == nullptr) {
    return false;
  }

  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());

  if (!HasExtension(extensions, VK_KHR_SURFACE_EXTENSION_NAME)) {
    FML_LOG(ERROR) << "Vulkan is missing " << VK_KHR_SURFACE_EXTENSION_NAME;
    return false;
  }
  instance_extensions_.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
  for (const char* name : kSurfaceExtensions) {
    if (HasExtension(extensions, name)) {
      instance_extensions_.push_back(name);
    }
  }
  if (instance_extensions_.size() == 1) {
    FML_LOG(ERROR) << "Vulkan has no window system extensions";
    return false;
  }

  VkApplicationInfo application_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "Flutter",
      .pEngineName = "Flutter",
      .apiVersion = kApiVersion,
  };
  VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &application_info,
      .enabledExtensionCount =
          static_cast<uint32_t>(instance_extensions_.size()),
      .ppEnabledExtensionNames = instance_extensions_.data(),
  };
  VkResult result = vkCreateInstance(&instance_info, nullptr, &instance_);
  if (result != VK_SUCCESS) {
    FML_LOG(ERROR) << "Failed to create Vulkan instance: " << result;
    return false;
  }

#define LOAD_INSTANCE_FUNCTION(name)                                         \
  name =                                                                     \
      reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance_, #name)); \
  if (name == nullptr) {                                                     \
    FML_LOG(ERROR) << "Vulkan is missing " #name;                            \
    return false;                                                            \
  }
  LOAD_INSTANCE_FUNCTION(vkDestroyInstance);
  LOAD_INSTANCE_FUNCTION(vkEnumeratePhysicalDevices);
  LOAD_INSTANCE_FUNCTION(vkGetPhysicalDeviceProperties2);
  LOAD_INSTANCE_FUNCTION(vkGetPhysicalDeviceFeatures2);
  LOAD_INSTANCE_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties);
  LOAD_INSTANCE_FUNCTION(vkGetPhysicalDeviceFormatProperties2);
  LOAD_INSTANCE_FUNCTION(vkGetPhysicalDeviceImageFormatProperties2);
  LOAD_INSTANCE_FUNCTION(vkGetPhysicalDeviceMemoryProperties);
  LOAD_INSTANCE_FUNCTION(vkEnumerateDeviceExtensionProperties);
  LOAD_INSTANCE_FUNCTION(vkCreateDevice);
  LOAD_INSTANCE_FUNCTION(vkGetDeviceProcAddr);
#undef LOAD_INSTANCE_FUNCTION

  return true;
}

bool VulkanDevice::PickPhysicalDevice(GpuDevice* gpu) {
  struct stat render_node;
  if (stat(gpu->path().c_str(), &render_node) != 0) {
    FML_LOG(ERROR) << "Failed to stat " << gpu->path();
    return false;
  }

  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance_, &count, nullptr);
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance_, &count, devices.data());

  // Vulkan must render on the same GPU as OpenGL so the dmabufs can be shared
  // without copies.
  for (VkPhysicalDevice device : devices) {
    uint32_t n_extensions = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &n_extensions,
                                         nullptr);
    std::vector<VkExtensionProperties> extensions(n_extensions);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &n_extensions,
                                         extensions.data());
    if (!HasExtension(extensions, VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME)) {
      continue;
    }

    VkPhysicalDeviceDrmPropertiesEXT drm = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT,
    };
    VkPhysicalDeviceProperties2 properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &drm,
    };
    vkGetPhysicalDeviceProperties2(device, &properties);
    if (!drm.hasRender ||
        makedev(drm.renderMajor, drm.renderMinor) != render_node.st_rdev) {
      continue;
    }
    if (properties.properties.apiVersion < kApiVersion) {
      FML_LOG(ERROR) << "Vulkan 1.1 is not supported by "
                     << properties.properties.deviceName;
      return false;
    }

    for (const char* name : kRequiredDeviceExtensions) {
      if (!HasExtension(extensions, name)) {
        FML_LOG(ERROR) << properties.properties.deviceName
                       << " is missing Vulkan extension " << name;
        return false;
      }
      device_extensions_.push_back(name);
    }
    // Required by VK_EXT_image_drm_format_modifier before Vulkan 1.2.
    if (properties.properties.apiVersion < VK_API_VERSION_1_2) {
      if (!HasExtension(extensions, VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME)) {
        return false;
      }
      device_extensions_.push_back(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
    }

    physical_device_ = device;
    name_ = properties.properties.deviceName;
    return true;
  }

  FML_LOG(ERROR) << "No Vulkan device found for " << gpu->path();
  return false;
}

bool VulkanDevice::CreateDevice() {
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &count,
                                           families.data());
  // Impeller uses one queue for everything. Graphics and compute queues
  // support transfers.
  constexpr VkQueueFlags kQueueFlags =
      VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
  bool found_queue = false;
  for (uint32_t i = 0; i < count; i++) {
    if ((families[i].queueFlags & kQueueFlags) == kQueueFlags) {
      queue_family_index_ = i;
      found_queue = true;
      break;
    }
  }
  if (!found_queue) {
    FML_LOG(ERROR) << "No Vulkan graphics queue";
    return false;
  }

  // Impeller assumes the features it would have enabled itself are enabled.
  VkPhysicalDevice16BitStorageFeatures storage_16bit = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
  };
  VkPhysicalDeviceFeatures2 supported = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &storage_16bit,
  };
  vkGetPhysicalDeviceFeatures2(physical_device_, &supported);

  VkPhysicalDevice16BitStorageFeatures enabled_storage_16bit = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
      .uniformAndStorageBuffer16BitAccess =
          storage_16bit.uniformAndStorageBuffer16BitAccess,
  };
  VkPhysicalDeviceFeatures2 enabled = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &enabled_storage_16bit,
  };
  enabled.features.fillModeNonSolid = supported.features.fillModeNonSolid;
  enabled.features.samplerAnisotropy = supported.features.samplerAnisotropy;
  enabled.features.textureCompressionETC2 =
      supported.features.textureCompressionETC2;
  enabled.features.textureCompressionASTC_LDR =
      supported.features.textureCompressionASTC_LDR;
  enabled.features.textureCompressionBC =
      supported.features.textureCompressionBC;

  float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = queue_family_index_,
      .queueCount = 1,
      .pQueuePriorities = &priority,
  };
  VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &enabled,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_info,
      .enabledExtensionCount = static_cast<uint32_t>(device_extensions_.size()),
      .ppEnabledExtensionNames = device_extensions_.data(),
  };
  VkResult result =
      vkCreateDevice(physical_device_, &device_info, nullptr, &device_);
  if (result != VK_SUCCESS) {
    FML_LOG(ERROR) << "Failed to create Vulkan device: " << result;
    return false;
  }
  return true;
}

bool VulkanDevice::LoadDeviceFunctions() {
#define LOAD_DEVICE_FUNCTION(name)                                          \
  name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device_, #name)); \
  if (name == nullptr) {                                                    \
    FML_LOG(ERROR) << "Vulkan is missing " #name;                           \
    return false;                                                           \
  }
  LOAD_DEVICE_FUNCTION(vkDestroyDevice);
  LOAD_DEVICE_FUNCTION(vkGetDeviceQueue);
  LOAD_DEVICE_FUNCTION(vkQueueSubmit);
  LOAD_DEVICE_FUNCTION(vkQueueWaitIdle);
  LOAD_DEVICE_FUNCTION(vkDeviceWaitIdle);
  LOAD_DEVICE_FUNCTION(vkCreateImage);
  LOAD_DEVICE_FUNCTION(vkDestroyImage);
  LOAD_DEVICE_FUNCTION(vkGetImageMemoryRequirements);
  LOAD_DEVICE_FUNCTION(vkAllocateMemory);
  LOAD_DEVICE_FUNCTION(vkFreeMemory);
  LOAD_DEVICE_FUNCTION(vkBindImageMemory);
  LOAD_DEVICE_FUNCTION(vkGetMemoryFdPropertiesKHR);
  LOAD_DEVICE_FUNCTION(vkCreateCommandPool);
  LOAD_DEVICE_FUNCTION(vkDestroyCommandPool);
  LOAD_DEVICE_FUNCTION(vkAllocateCommandBuffers);
  LOAD_DEVICE_FUNCTION(vkBeginCommandBuffer);
  LOAD_DEVICE_FUNCTION(vkEndCommandBuffer);
  LOAD_DEVICE_FUNCTION(vkCmdPipelineBarrier);
  LOAD_DEVICE_FUNCTION(vkCreateFence);
  LOAD_DEVICE_FUNCTION(vkDestroyFence);
  LOAD_DEVICE_FUNCTION(vkWaitForFences);
  LOAD_DEVICE_FUNCTION(vkResetFences);
  LOAD_DEVICE_FUNCTION(vkCreateSemaphore);
  LOAD_DEVICE_FUNCTION(vkDestroySemaphore);
  LOAD_DEVICE_FUNCTION(vkGetSemaphoreFdKHR);
#undef LOAD_DEVICE_FUNCTION

  vkGetDeviceQueue(device_, queue_family_index_, 0, &queue_);
  return true;
}

void VulkanDevice::FindModifiers() {
  VkDrmFormatModifierPropertiesListEXT list = {
      .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
  };
  VkFormatProperties2 format_properties = {
      .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
      .pNext = &list,
  };
  vkGetPhysicalDeviceFormatProperties2(physical_device_, kFormat,
                                       &format_properties);
  std::vector<VkDrmFormatModifierPropertiesEXT> properties(
      list.drmFormatModifierCount);
  list.pDrmFormatModifierProperties = properties.data();
  vkGetPhysicalDeviceFormatProperties2(physical_device_, kFormat,
                                       &format_properties);

  for (const VkDrmFormatModifierPropertiesEXT& property : properties) {
    if ((property.drmFormatModifierTilingFeatures & kFormatFeatures) !=
        kFormatFeatures) {
      continue;
    }

    // Check the modifier can be imported with the usage needed.
    VkPhysicalDeviceImageDrmFormatModifierInfoEXT modifier_info = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,
        .drmFormatModifier = property.drmFormatModifier,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkPhysicalDeviceExternalImageFormatInfo external_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        .pNext = &modifier_info,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };
    VkPhysicalDeviceImageFormatInfo2 format_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &external_info,
        .format = kFormat,
        .type = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        .usage = kImageUsage,
    };
    VkExternalImageFormatProperties external_properties = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
    };
    VkImageFormatProperties2 image_properties = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        .pNext = &external_properties,
    };
    if (vkGetPhysicalDeviceImageFormatProperties2(
            physical_device_, &format_info, &image_properties) != VK_SUCCESS) {
      continue;
    }
    if ((external_properties.externalMemoryProperties.externalMemoryFeatures &
         VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) == 0) {
      continue;
    }
    modifiers_.push_back(property.drmFormatModifier);
  }
}

std::unique_ptr<VulkanImage> VulkanDevice::ImportDmabuf(
    int width,
    int height,
    uint64_t modifier,
    const GpuDevice::DmabufPlane* planes,
    size_t n_planes) {
  std::unique_ptr<VulkanImage> image(new VulkanImage(this));

  VkSubresourceLayout layouts[GpuDevice::kMaxDmabufPlanes] = {};
  for (size_t i = 0; i < n_planes; i++) {
    layouts[i].offset = planes[i].offset;
    layouts[i].rowPitch = planes[i].stride;
  }
  VkImageDrmFormatModifierExplicitCreateInfoEXT modifier_info = {
      .sType =
          VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
      .drmFormatModifier = modifier,
      .drmFormatModifierPlaneCount = static_cast<uint32_t>(n_planes),
      .pPlaneLayouts = layouts,
  };
  VkExternalMemoryImageCreateInfo external_info = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .pNext = &modifier_info,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
  };
  VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = &external_info,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = kFormat,
      .extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
      .usage = kImageUsage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  VkResult result =
      vkCreateImage(device_, &image_info, nullptr, &image->image_);
  if (result != VK_SUCCESS) {
    FML_LOG(ERROR) << "Failed to create Vulkan image: " << result;
    return nullptr;
  }

  // All planes are in the same dmabuf.
  fml::UniqueFD fd(dup(planes[0].fd));
  if (!fd.is_valid()) {
    return nullptr;
  }
  VkMemoryFdPropertiesKHR fd_properties = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
  };
  result = vkGetMemoryFdPropertiesKHR(
      device_, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd.get(),
      &fd_properties);
  if (result != VK_SUCCESS) {
    FML_LOG(ERROR) << "Failed to get dmabuf memory properties: " << result;
    return nullptr;
  }
  VkMemoryRequirements requirements;
  vkGetImageMemoryRequirements(device_, image->image_, &requirements);
  uint32_t memory_types =
      requirements.memoryTypeBits & fd_properties.memoryTypeBits;
  if (memory_types == 0) {
    FML_LOG(ERROR) << "No Vulkan memory type for dmabuf";
    return nullptr;
  }

  VkMemoryDedicatedAllocateInfo dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .image = image->image_,
  };
  VkImportMemoryFdInfoKHR import_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .pNext = &dedicated_info,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      .fd = fd.get(),
  };
  VkMemoryAllocateInfo allocate_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &import_info,
      .allocationSize = requirements.size,
      .memoryTypeIndex = static_cast<uint32_t>(__builtin_ctz(memory_types)),
  };
  result = vkAllocateMemory(device_, &allocate_info, nullptr, &image->memory_);
  if (result != VK_SUCCESS) {
    FML_LOG(ERROR) << "Failed to import dmabuf into Vulkan: " << result;
    return nullptr;
  }
  // Vulkan owns the fd now.
  [[maybe_unused]] int imported_fd = fd.release();

  result = vkBindImageMemory(device_, image->image_, image->memory_, 0);
  if (result != VK_SUCCESS) {
    FML_LOG(ERROR) << "Failed to bind Vulkan image memory: " << result;
    return nullptr;
  }
  return image;
}

fml::UniqueFD VulkanDevice::ReleaseImages(const std::vector<VkImage>& images) {
  std::scoped_lock lock(queue_mutex_);

  Submission& submission = submissions_[next_submission_];
  next_submission_ = (next_submission_ + 1) % submissions_.size();
  if (vkWaitForFences(device_, 1, &submission.fence, VK_TRUE,
                      kFenceTimeoutNs) != VK_SUCCESS) {
    FML_LOG(ERROR) << "Timed out waiting for Vulkan";
    vkQueueWaitIdle(queue_);
  }
  vkResetFences(device_, 1, &submission.fence);

  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  vkBeginCommandBuffer(submission.command_buffer, &begin_info);
  // Impeller leaves the images it renders into in the general layout. They
  // are handed to the "foreign" queue family so other APIs see the rendered
  // contents. They are taken back implicitly when Impeller next renders into
  // them, as it discards their contents.
  std::vector<VkImageMemoryBarrier> barriers;
  for (VkImage image : images) {
    barriers.push_back({
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = queue_family_index_,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
        .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    });
  }
  vkCmdPipelineBarrier(
      submission.command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr,
      static_cast<uint32_t>(barriers.size()), barriers.data());
  vkEndCommandBuffer(submission.command_buffer);

  // The semaphore is signalled once everything submitted before it has
  // completed, including Impeller's rendering.
  VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &submission.command_buffer,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &submission.semaphore,
  };
  VkResult result = vkQueueSubmit(queue_, 1, &submit_info, submission.fence);
  if (result != VK_SUCCESS) {
    FML_LOG(ERROR) << "Failed to submit Vulkan image release: " << result;
    vkQueueWaitIdle(queue_);
    return fml::UniqueFD();
  }

  // Exporting a sync file resets the semaphore so it can be signalled again.
  VkSemaphoreGetFdInfoKHR fd_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
      .semaphore = submission.semaphore,
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
  };
  int fd = -1;
  result = vkGetSemaphoreFdKHR(device_, &fd_info, &fd);
  if (result != VK_SUCCESS) {
    FML_LOG(ERROR) << "Failed to export Vulkan sync file: " << result;
    vkQueueWaitIdle(queue_);
    return fml::UniqueFD();
  }
  // -1 means the semaphore has already been signalled.
  return fml::UniqueFD(fd);
}

void VulkanDevice::WaitIdle() {
  std::scoped_lock lock(queue_mutex_);
  vkDeviceWaitIdle(device_);
}

PFN_vkVoidFunction VulkanDevice::GetInstanceProcAddress(VkInstance instance,
                                                        const char* name) {
  if (g_device == nullptr) {
    return nullptr;
  }
  if (strcmp(name, "vkGetInstanceProcAddr") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(&GetInstanceProcAddress);
  }
  if (strcmp(name, "vkGetDeviceProcAddr") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddrLocked);
  }
  if (strcmp(name, "vkQueueSubmit") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(&QueueSubmitLocked);
  }
  if (strcmp(name, "vkQueueWaitIdle") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(&QueueWaitIdleLocked);
  }
  if (strcmp(name, "vkDeviceWaitIdle") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(&DeviceWaitIdleLocked);
  }
  // Submitting with these isn't serialized.
  if (strncmp(name, "vkQueueSubmit2", strlen("vkQueueSubmit2")) == 0) {
    return nullptr;
  }
  return g_device->vkGetInstanceProcAddr(instance, name);
}

PFN_vkVoidFunction VulkanDevice::GetDeviceProcAddrLocked(VkDevice device,
                                                         const char* name) {
  if (strcmp(name, "vkGetDeviceProcAddr") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddrLocked);
  }
  if (strcmp(name, "vkQueueSubmit") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(&QueueSubmitLocked);
  }
  if (strcmp(name, "vkQueueWaitIdle") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(&QueueWaitIdleLocked);
  }
  if (strcmp(name, "vkDeviceWaitIdle") == 0) {
    return reinterpret_cast<PFN_vkVoidFunction>(&DeviceWaitIdleLocked);
  }
  if (strncmp(name, "vkQueueSubmit2", strlen("vkQueueSubmit2")) == 0) {
    return nullptr;
  }
  return g_device->vkGetDeviceProcAddr(device, name);
}

VkResult VulkanDevice::QueueSubmitLocked(VkQueue queue,
                                         uint32_t count,
                                         const VkSubmitInfo* submits,
                                         VkFence fence) {
  std::scoped_lock lock(g_device->queue_mutex_);
  return g_device->vkQueueSubmit(queue, count, submits, fence);
}

VkResult VulkanDevice::QueueWaitIdleLocked(VkQueue queue) {
  std::scoped_lock lock(g_device->queue_mutex_);
  return g_device->vkQueueWaitIdle(queue);
}

VkResult VulkanDevice::DeviceWaitIdleLocked(VkDevice device) {
  std::scoped_lock lock(g_device->queue_mutex_);
  return g_device->vkDeviceWaitIdle(device);
}

}  // namespace flutter
