// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland/dmabuf_buffer.h"

#include <drm_fourcc.h>
#include <unistd.h>

#include "flutter/fml/logging.h"
#include "flutter/fml/unique_fd.h"

namespace flutter {

const uint32_t DmabufBuffer::kFormat = DRM_FORMAT_ARGB8888;

std::unique_ptr<DmabufBuffer> DmabufBuffer::Create(
    EGLManager* egl,
    zwp_linux_dmabuf_v1* dmabuf,
    const std::vector<uint64_t>& modifiers,
    int width,
    int height,
    bool depth_stencil) {
  std::unique_ptr<DmabufBuffer> buffer(new DmabufBuffer(egl, width, height));
  if (!buffer->Initialize(dmabuf, modifiers, depth_stencil)) {
    return nullptr;
  }
  return buffer;
}

DmabufBuffer::DmabufBuffer(EGLManager* egl, int width, int height)
    : RenderBuffer(egl, width, height) {}

DmabufBuffer::~DmabufBuffer() {
  if (texture_ != 0 && egl()->IsRenderContextCurrent()) {
    glDeleteTextures(1, &texture_);
  }
  if (image_ != EGL_NO_IMAGE_KHR) {
    egl()->gpu()->DestroyImage(egl()->gpu()->display(), image_);
  }
  if (bo_ != nullptr) {
    gbm_bo_destroy(bo_);
  }
}

bool DmabufBuffer::Initialize(zwp_linux_dmabuf_v1* dmabuf,
                              const std::vector<uint64_t>& modifiers,
                              bool depth_stencil) {
  // Use explicit modifiers if both the compositor and EGL support them, as
  // this allows the driver to pick an optimal (e.g. compressed or tiled)
  // layout that the compositor can still read.
  std::vector<uint64_t> explicit_modifiers;
  if (egl()->gpu()->supports_dmabuf_modifiers()) {
    for (uint64_t modifier : modifiers) {
      if (modifier != DRM_FORMAT_MOD_INVALID) {
        explicit_modifiers.push_back(modifier);
      }
    }
  }

  uint64_t modifier = DRM_FORMAT_MOD_INVALID;
  if (!explicit_modifiers.empty()) {
    bo_ = gbm_bo_create_with_modifiers(egl()->gpu()->gbm(), width(), height(),
                                       kFormat, explicit_modifiers.data(),
                                       explicit_modifiers.size());
    if (bo_ != nullptr) {
      modifier = gbm_bo_get_modifier(bo_);
    }
  }
  if (bo_ == nullptr) {
    bo_ = gbm_bo_create(egl()->gpu()->gbm(), width(), height(), kFormat,
                        GBM_BO_USE_RENDERING);
  }
  if (bo_ == nullptr) {
    FML_LOG(ERROR) << "Failed to allocate " << width() << "x" << height()
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
  fml::UniqueFD fd(gbm_bo_get_fd(bo_));
  if (!fd.is_valid()) {
    FML_LOG(ERROR) << "Failed to export buffer";
    return false;
  }

  modifier_ = modifier;
  GpuDevice::DmabufPlane planes[GpuDevice::kMaxDmabufPlanes];
  for (int i = 0; i < n_planes; i++) {
    planes[i] = {fd.get(), gbm_bo_get_offset(bo_, i),
                 gbm_bo_get_stride_for_plane(bo_, i)};
  }
  image_ = egl()->gpu()->ImportDmabuf(width(), height(), kFormat, modifier,
                                      planes, n_planes);
  if (image_ == EGL_NO_IMAGE_KHR) {
    FML_LOG(ERROR) << "Failed to import buffer into EGL: 0x" << std::hex
                   << eglGetError();
    return false;
  }

  glGenTextures(1, &texture_);
  glBindTexture(GL_TEXTURE_2D, texture_);
  egl()->gpu()->ImageTargetTexture2D(GL_TEXTURE_2D, image_);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (!CreateFramebuffer(GL_TEXTURE_2D, texture_, depth_stencil)) {
    return false;
  }

  zwp_linux_buffer_params_v1* params =
      zwp_linux_dmabuf_v1_create_params(dmabuf);
  for (int i = 0; i < n_planes; i++) {
    zwp_linux_buffer_params_v1_add(params, fd.get(), i,
                                   gbm_bo_get_offset(bo_, i),
                                   gbm_bo_get_stride_for_plane(bo_, i),
                                   modifier >> 32, modifier & 0xffffffff);
  }
  SetBuffer(zwp_linux_buffer_params_v1_create_immed(params, width(), height(),
                                                    kFormat, 0));
  zwp_linux_buffer_params_v1_destroy(params);

  return true;
}

}  // namespace flutter
