// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/shm_buffer.h"

#include <drm_fourcc.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

#include "flutter/fml/eintr_wrapper.h"
#include "flutter/fml/logging.h"
#include "flutter/fml/unique_fd.h"

namespace flutter {

namespace {

constexpr int kBytesPerPixel = 4;

}  // namespace

std::unique_ptr<ShmBuffer> ShmBuffer::Create(EGLManager* egl,
                                             wl_shm* shm,
                                             int width,
                                             int height,
                                             bool depth_stencil) {
  std::unique_ptr<ShmBuffer> buffer(new ShmBuffer(egl, width, height));
  if (!buffer->Initialize(shm, depth_stencil)) {
    return nullptr;
  }
  return buffer;
}

ShmBuffer::ShmBuffer(EGLManager* egl, int width, int height)
    : RenderBuffer(egl, width, height) {}

ShmBuffer::~ShmBuffer() {
  if (data_ != nullptr) {
    munmap(data_, size_);
  }
  if (color_ != 0 && egl()->IsRenderContextCurrent()) {
    glDeleteRenderbuffers(1, &color_);
  }
}

uint64_t ShmBuffer::modifier() const {
  return DRM_FORMAT_MOD_LINEAR;
}

bool ShmBuffer::Initialize(wl_shm* shm, bool depth_stencil) {
  int stride = width() * kBytesPerPixel;
  size_ = static_cast<size_t>(stride) * height();

  fml::UniqueFD fd(memfd_create("flutter-shm-buffer", MFD_CLOEXEC));
  if (!fd.is_valid()) {
    FML_LOG(ERROR) << "Failed to create shared memory: " << strerror(errno);
    return false;
  }
  if (FML_HANDLE_EINTR(ftruncate(fd.get(), size_)) != 0) {
    FML_LOG(ERROR) << "Failed to size shared memory: " << strerror(errno);
    return false;
  }
  void* data =
      mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
  if (data == MAP_FAILED) {
    FML_LOG(ERROR) << "Failed to map shared memory: " << strerror(errno);
    return false;
  }
  data_ = data;

  glGenRenderbuffers(1, &color_);
  glBindRenderbuffer(GL_RENDERBUFFER, color_);
  glRenderbufferStorage(GL_RENDERBUFFER, kGLFormat, width(), height());
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  if (!CreateFramebuffer(GL_RENDERBUFFER, color_, depth_stencil)) {
    return false;
  }

  wl_shm_pool* pool = wl_shm_create_pool(shm, fd.get(), size_);
  SetBuffer(wl_shm_pool_create_buffer(pool, 0, width(), height(), stride,
                                      WL_SHM_FORMAT_ARGB8888));
  wl_shm_pool_destroy(pool);
  return true;
}

bool ShmBuffer::PrepareToPresent() {
  GLint previous_framebuffer = 0;
  GLint previous_alignment = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
  glGetIntegerv(GL_PACK_ALIGNMENT, &previous_alignment);

  // GL rows start at the bottom, but Flutter renders into framebuffers upside
  // down, so the rows are read in the order Wayland expects.
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer());
  glPixelStorei(GL_PACK_ALIGNMENT, kBytesPerPixel);
  auto* pixels = static_cast<uint8_t*>(data_);
  if (egl()->supports_read_bgra()) {
    // Matches the byte order of WL_SHM_FORMAT_ARGB8888 on little-endian
    // machines.
    glReadPixels(0, 0, width(), height(), GL_BGRA_EXT, GL_UNSIGNED_BYTE,
                 pixels);
  } else {
    glReadPixels(0, 0, width(), height(), GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (size_t i = 0; i < size_; i += kBytesPerPixel) {
      std::swap(pixels[i], pixels[i + 2]);
    }
  }
  GLenum error = glGetError();

  glPixelStorei(GL_PACK_ALIGNMENT, previous_alignment);
  glBindFramebuffer(GL_FRAMEBUFFER, previous_framebuffer);

  if (error != GL_NO_ERROR) {
    FML_LOG(ERROR) << "Failed to read frame: 0x" << std::hex << error;
    return false;
  }
  return true;
}

}  // namespace flutter
