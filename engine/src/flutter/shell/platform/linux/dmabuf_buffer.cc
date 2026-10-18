// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/dmabuf_buffer.h"

#include <drm_fourcc.h>
#include <unistd.h>

#include "flutter/fml/logging.h"
#include "flutter/fml/unique_fd.h"

namespace flutter {

namespace {

// The EGL attributes for each dmabuf plane.
struct PlaneAttributes {
  EGLint fd;
  EGLint offset;
  EGLint pitch;
  EGLint modifier_lo;
  EGLint modifier_hi;
};

constexpr PlaneAttributes kPlaneAttributes[] = {
    {EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE0_OFFSET_EXT,
     EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
     EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT},
    {EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT,
     EGL_DMA_BUF_PLANE1_PITCH_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
     EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT},
    {EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT,
     EGL_DMA_BUF_PLANE2_PITCH_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
     EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT},
    {EGL_DMA_BUF_PLANE3_FD_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT,
     EGL_DMA_BUF_PLANE3_PITCH_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT,
     EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT},
};

constexpr int kMaxPlanes = 4;

}  // namespace

const uint32_t DmabufBuffer::kFormat = DRM_FORMAT_ARGB8888;
const uint32_t DmabufBuffer::kGLFormat = GL_RGBA8_OES;

const wl_buffer_listener DmabufBuffer::kBufferListener = {
    .release = BufferRelease,
};

std::unique_ptr<DmabufBuffer> DmabufBuffer::Create(
    EGLManager* egl,
    zwp_linux_dmabuf_v1* dmabuf,
    const std::vector<uint64_t>& modifiers,
    int width,
    int height,
    bool depth_stencil) {
  std::unique_ptr<DmabufBuffer> buffer(new DmabufBuffer(egl));
  buffer->width_ = width;
  buffer->height_ = height;
  if (!buffer->Initialize(dmabuf, modifiers, depth_stencil)) {
    return nullptr;
  }
  return buffer;
}

DmabufBuffer::DmabufBuffer(EGLManager* egl) : egl_(egl) {}

DmabufBuffer::~DmabufBuffer() {
  if (buffer_ != nullptr) {
    wl_buffer_destroy(buffer_);
  }
  if (egl_->IsRenderContextCurrent()) {
    if (framebuffer_ != 0) {
      glDeleteFramebuffers(1, &framebuffer_);
    }
    if (depth_stencil_ != 0) {
      glDeleteRenderbuffers(1, &depth_stencil_);
    }
    if (texture_ != 0) {
      glDeleteTextures(1, &texture_);
    }
  }
  if (image_ != EGL_NO_IMAGE_KHR) {
    egl_->gpu()->DestroyImage(egl_->gpu()->display(), image_);
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
  if (egl_->gpu()->supports_dmabuf_modifiers()) {
    for (uint64_t modifier : modifiers) {
      if (modifier != DRM_FORMAT_MOD_INVALID) {
        explicit_modifiers.push_back(modifier);
      }
    }
  }

  uint64_t modifier = DRM_FORMAT_MOD_INVALID;
  if (!explicit_modifiers.empty()) {
    bo_ = gbm_bo_create_with_modifiers(egl_->gpu()->gbm(), width_, height_,
                                       kFormat, explicit_modifiers.data(),
                                       explicit_modifiers.size());
    if (bo_ != nullptr) {
      modifier = gbm_bo_get_modifier(bo_);
    }
  }
  if (bo_ == nullptr) {
    bo_ = gbm_bo_create(egl_->gpu()->gbm(), width_, height_, kFormat,
                        GBM_BO_USE_RENDERING);
  }
  if (bo_ == nullptr) {
    FML_LOG(ERROR) << "Failed to allocate " << width_ << "x" << height_
                   << " buffer";
    return false;
  }

  int n_planes = gbm_bo_get_plane_count(bo_);
  if (n_planes < 1 || n_planes > kMaxPlanes) {
    FML_LOG(ERROR) << "Unsupported buffer plane count " << n_planes;
    return false;
  }

  // All planes of a GBM buffer are in the same dmabuf.
  fml::UniqueFD fd(gbm_bo_get_fd(bo_));
  if (!fd.is_valid()) {
    FML_LOG(ERROR) << "Failed to export buffer";
    return false;
  }

  bool has_modifier = modifier != DRM_FORMAT_MOD_INVALID;
  std::vector<EGLint> attributes = {EGL_WIDTH,
                                    width_,
                                    EGL_HEIGHT,
                                    height_,
                                    EGL_LINUX_DRM_FOURCC_EXT,
                                    static_cast<EGLint>(kFormat)};
  for (int i = 0; i < n_planes; i++) {
    const PlaneAttributes& plane = kPlaneAttributes[i];
    attributes.insert(
        attributes.end(),
        {plane.fd, fd.get(), plane.offset,
         static_cast<EGLint>(gbm_bo_get_offset(bo_, i)), plane.pitch,
         static_cast<EGLint>(gbm_bo_get_stride_for_plane(bo_, i))});
    if (has_modifier) {
      attributes.insert(
          attributes.end(),
          {plane.modifier_lo, static_cast<EGLint>(modifier & 0xffffffff),
           plane.modifier_hi, static_cast<EGLint>(modifier >> 32)});
    }
  }
  attributes.push_back(EGL_NONE);

  image_ = egl_->gpu()->CreateImage(egl_->gpu()->display(), EGL_NO_CONTEXT,
                                    EGL_LINUX_DMA_BUF_EXT, nullptr,
                                    attributes.data());
  if (image_ == EGL_NO_IMAGE_KHR) {
    FML_LOG(ERROR) << "Failed to import buffer into EGL: 0x" << std::hex
                   << eglGetError();
    return false;
  }

  glGenTextures(1, &texture_);
  glBindTexture(GL_TEXTURE_2D, texture_);
  egl_->gpu()->ImageTargetTexture2D(GL_TEXTURE_2D, image_);
  glBindTexture(GL_TEXTURE_2D, 0);

  glGenFramebuffers(1, &framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture_, 0);
  if (depth_stencil) {
    glGenRenderbuffers(1, &depth_stencil_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_stencil_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, width_,
                          height_);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth_stencil_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, depth_stencil_);
  }
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    FML_LOG(ERROR) << "Buffer framebuffer incomplete: 0x" << std::hex << status;
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
  buffer_ = zwp_linux_buffer_params_v1_create_immed(params, width_, height_,
                                                    kFormat, 0);
  zwp_linux_buffer_params_v1_destroy(params);
  wl_buffer_add_listener(buffer_, &kBufferListener, this);

  return true;
}

void DmabufBuffer::BufferRelease(void* data, wl_buffer* buffer) {
  static_cast<DmabufBuffer*>(data)->busy_ = false;
}

}  // namespace flutter
