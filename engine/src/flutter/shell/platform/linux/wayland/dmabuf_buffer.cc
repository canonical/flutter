// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland/dmabuf_buffer.h"

#include <drm_fourcc.h>

namespace flutter {

const uint32_t DmabufBuffer::kFormat = DRM_FORMAT_ARGB8888;

std::unique_ptr<DmabufBuffer> DmabufBuffer::Create(
    EGLManager* egl,
    VulkanDevice* vulkan,
    zwp_linux_dmabuf_v1* dmabuf,
    const std::vector<uint64_t>& modifiers,
    int width,
    int height,
    bool depth_stencil) {
  std::unique_ptr<DmabufBuffer> buffer(new DmabufBuffer(egl, width, height));
  if (!buffer->Initialize(vulkan, dmabuf, modifiers, depth_stencil)) {
    return nullptr;
  }
  return buffer;
}

DmabufBuffer::DmabufBuffer(EGLManager* egl, int width, int height)
    : RenderBuffer(egl, width, height) {}

DmabufBuffer::~DmabufBuffer() = default;

bool DmabufBuffer::Initialize(VulkanDevice* vulkan,
                              zwp_linux_dmabuf_v1* dmabuf,
                              const std::vector<uint64_t>& modifiers,
                              bool depth_stencil) {
  // Use explicit modifiers if both the compositor and EGL support them, as
  // this allows the driver to pick an optimal (e.g. compressed or tiled)
  // layout that the compositor can still read.
  image_ = DmabufImage::Create(egl(), vulkan,
                               egl()->gpu()->supports_dmabuf_modifiers()
                                   ? modifiers
                                   : std::vector<uint64_t>(),
                               width(), height());
  if (!image_) {
    return false;
  }

  if (!CreateFramebuffer(GL_TEXTURE_2D, image_->texture(), depth_stencil)) {
    return false;
  }

  uint64_t modifier = image_->modifier();
  zwp_linux_buffer_params_v1* params =
      zwp_linux_dmabuf_v1_create_params(dmabuf);
  for (size_t i = 0; i < image_->n_planes(); i++) {
    const GpuDevice::DmabufPlane& plane = image_->planes()[i];
    zwp_linux_buffer_params_v1_add(params, plane.fd, i, plane.offset,
                                   plane.stride, modifier >> 32,
                                   modifier & 0xffffffff);
  }
  SetBuffer(zwp_linux_buffer_params_v1_create_immed(params, width(), height(),
                                                    kFormat, 0));
  zwp_linux_buffer_params_v1_destroy(params);

  return true;
}

}  // namespace flutter
