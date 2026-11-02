// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DMABUF_IMAGE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DMABUF_IMAGE_H_

#include <gbm.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/fml/unique_fd.h"
#include "flutter/shell/platform/linux/egl_manager.h"
#include "flutter/shell/platform/linux/gpu_device.h"
#include "flutter/shell/platform/linux/vulkan_device.h"

namespace flutter {

// An image in a dmabuf allocated with GBM, imported as a GL texture and, if
// Flutter renders with Vulkan, as a Vulkan image.
class DmabufImage {
 public:
  // Allocates an image of DRM_FORMAT_ARGB8888.
  //
  // |modifiers| are the layouts the image may have. If empty, the driver
  // chooses the layout implicitly.
  //
  // If |vulkan| is set, the image is also imported into Vulkan and one of
  // |modifiers| that Vulkan supports is used.
  //
  // Must be called with the render context current.
  static std::unique_ptr<DmabufImage> Create(
      EGLManager* egl,
      VulkanDevice* vulkan,
      const std::vector<uint64_t>& modifiers,
      int width,
      int height);

  // The GL texture is only deleted if the render context is current,
  // otherwise it is freed when the context is destroyed.
  ~DmabufImage();

  // The dmabuf holding the image.
  int fd() const { return fd_.get(); }

  // DRM_FORMAT_MOD_INVALID if the driver chose the layout implicitly.
  uint64_t modifier() const { return modifier_; }

  size_t n_planes() const { return n_planes_; }
  const GpuDevice::DmabufPlane* planes() const { return planes_; }

  GLuint texture() const { return texture_; }

  // The image in Vulkan, or VK_NULL_HANDLE if it hasn't been imported.
  VkImage vulkan_image() const {
    return vulkan_image_ ? vulkan_image_->image() : VK_NULL_HANDLE;
  }

  // The DRM format of the image.
  static const uint32_t kFormat;

 private:
  explicit DmabufImage(EGLManager* egl) : egl_(egl) {}

  bool Initialize(VulkanDevice* vulkan,
                  const std::vector<uint64_t>& modifiers,
                  int width,
                  int height);

  EGLManager* egl_;
  gbm_bo* bo_ = nullptr;
  fml::UniqueFD fd_;
  uint64_t modifier_ = 0;
  size_t n_planes_ = 0;
  GpuDevice::DmabufPlane planes_[GpuDevice::kMaxDmabufPlanes] = {};
  EGLImageKHR image_ = EGL_NO_IMAGE_KHR;
  GLuint texture_ = 0;
  std::unique_ptr<VulkanImage> vulkan_image_;

  FML_DISALLOW_COPY_AND_ASSIGN(DmabufImage);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DMABUF_IMAGE_H_
