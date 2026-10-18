// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/render_buffer.h"

#include "flutter/fml/logging.h"

namespace flutter {

const uint32_t RenderBuffer::kGLFormat = GL_RGBA8_OES;

const wl_buffer_listener RenderBuffer::kBufferListener = {
    .release = BufferRelease,
};

RenderBuffer::RenderBuffer(EGLManager* egl, int width, int height)
    : egl_(egl), width_(width), height_(height) {}

RenderBuffer::~RenderBuffer() {
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
  }
}

bool RenderBuffer::CreateFramebuffer(GLenum color_target,
                                     GLuint color,
                                     bool depth_stencil) {
  glGenFramebuffers(1, &framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  if (color_target == GL_RENDERBUFFER) {
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, color);
  } else {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, color_target,
                           color, 0);
  }
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
  return true;
}

void RenderBuffer::SetBuffer(wl_buffer* buffer) {
  buffer_ = buffer;
  wl_buffer_add_listener(buffer_, &kBufferListener, this);
}

void RenderBuffer::BufferRelease(void* data, wl_buffer* buffer) {
  static_cast<RenderBuffer*>(data)->busy_ = false;
}

}  // namespace flutter
