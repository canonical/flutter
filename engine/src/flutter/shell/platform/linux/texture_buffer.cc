// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/texture_buffer.h"

#include <drm_fourcc.h>

namespace flutter {

std::unique_ptr<TextureBuffer> TextureBuffer::Create(EGLManager* egl,
                                                     int width,
                                                     int height,
                                                     bool depth_stencil) {
  std::unique_ptr<TextureBuffer> buffer(new TextureBuffer(egl, width, height));
  if (!buffer->Initialize(depth_stencil)) {
    return nullptr;
  }
  return buffer;
}

TextureBuffer::TextureBuffer(EGLManager* egl, int width, int height)
    : RenderBuffer(egl, width, height) {}

TextureBuffer::~TextureBuffer() {
  if (texture_ != 0 && egl()->IsRenderContextCurrent()) {
    glDeleteTextures(1, &texture_);
  }
}

uint64_t TextureBuffer::modifier() const {
  return DRM_FORMAT_MOD_INVALID;
}

bool TextureBuffer::Initialize(bool depth_stencil) {
  glGenTextures(1, &texture_);
  glBindTexture(GL_TEXTURE_2D, texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width(), height(), 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glBindTexture(GL_TEXTURE_2D, 0);
  return CreateFramebuffer(GL_TEXTURE_2D, texture_, depth_stencil);
}

bool TextureBuffer::PrepareToPresent() {
  // The display backend reads the texture in the same context, so GL
  // ordering ensures rendering has completed.
  return true;
}

}  // namespace flutter
