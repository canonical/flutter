// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/dmabuf_image.h"

#include <drm_fourcc.h>

#include <algorithm>

#include "flutter/fml/logging.h"

namespace flutter {

const uint32_t DmabufImage::kFormat = DRM_FORMAT_ARGB8888;

std::unique_ptr<DmabufImage> DmabufImage::Create(
    EGLManager* egl,
    VulkanDevice* vulkan,
    const std::vector<uint64_t>& modifiers,
    int width,
    int height) {
  std::unique_ptr<DmabufImage> image(new DmabufImage(egl));
  if (!image->Initialize(vulkan, modifiers, width, height)) {
    return nullptr;
  }
  return image;
}

DmabufImage::~DmabufImage() {
  vulkan_image_.reset();
  if (texture_ != 0 && egl_->IsRenderContextCurrent()) {
    glDeleteTextures(1, &texture_);
  }
  if (image_ != EGL_NO_IMAGE_KHR) {
    egl_->gpu()->DestroyImage(egl_->gpu()->display(), image_);
  }
  if (bo_ != nullptr) {
    gbm_bo_destroy(bo_);
  }
}

bool DmabufImage::Initialize(VulkanDevice* vulkan,
                             const std::vector<uint64_t>& modifiers,
                             int width,
                             int height) {
  GpuDevice* gpu = egl_->gpu();

  std::vector<uint64_t> explicit_modifiers;
  for (uint64_t modifier : modifiers) {
    if (modifier == DRM_FORMAT_MOD_INVALID) {
      continue;
    }
    if (vulkan != nullptr &&
        std::find(vulkan->modifiers().begin(), vulkan->modifiers().end(),
                  modifier) == vulkan->modifiers().end()) {
      continue;
    }
    explicit_modifiers.push_back(modifier);
  }

  modifier_ = DRM_FORMAT_MOD_INVALID;
  if (!explicit_modifiers.empty()) {
    bo_ = gbm_bo_create_with_modifiers(gpu->gbm(), width, height, kFormat,
                                       explicit_modifiers.data(),
                                       explicit_modifiers.size());
    if (bo_ != nullptr) {
      modifier_ = gbm_bo_get_modifier(bo_);
    }
  }
  // Vulkan can only import images with a known layout.
  if (bo_ == nullptr && vulkan == nullptr) {
    bo_ =
        gbm_bo_create(gpu->gbm(), width, height, kFormat, GBM_BO_USE_RENDERING);
  }
  if (bo_ == nullptr) {
    FML_LOG(ERROR) << "Failed to allocate " << width << "x" << height
                   << " buffer";
    return false;
  }

  int n_planes = gbm_bo_get_plane_count(bo_);
  if (n_planes < 1 ||
      n_planes > static_cast<int>(GpuDevice::kMaxDmabufPlanes)) {
    FML_LOG(ERROR) << "Unsupported buffer plane count " << n_planes;
    return false;
  }

  // All planes of a GBM buffer are in the same dmabuf.
  fd_.reset(gbm_bo_get_fd(bo_));
  if (!fd_.is_valid()) {
    FML_LOG(ERROR) << "Failed to export buffer";
    return false;
  }

  n_planes_ = n_planes;
  for (int i = 0; i < n_planes; i++) {
    planes_[i] = {fd_.get(), gbm_bo_get_offset(bo_, i),
                  gbm_bo_get_stride_for_plane(bo_, i)};
  }
  image_ =
      gpu->ImportDmabuf(width, height, kFormat, modifier_, planes_, n_planes_);
  if (image_ == EGL_NO_IMAGE_KHR) {
    FML_LOG(ERROR) << "Failed to import buffer into EGL: 0x" << std::hex
                   << eglGetError();
    return false;
  }

  glGenTextures(1, &texture_);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gpu->ImageTargetTexture2D(GL_TEXTURE_2D, image_);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (vulkan != nullptr) {
    vulkan_image_ =
        vulkan->ImportDmabuf(width, height, modifier_, planes_, n_planes_);
    if (!vulkan_image_) {
      return false;
    }
  }

  return true;
}

}  // namespace flutter
