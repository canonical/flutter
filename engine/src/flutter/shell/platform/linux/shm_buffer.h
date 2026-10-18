// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_SHM_BUFFER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_SHM_BUFFER_H_

#include <wayland-client.h>

#include <cstddef>
#include <memory>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/render_buffer.h"

namespace flutter {

// A buffer that Flutter renders into on the GPU, which is then copied into
// shared memory for the compositor (wl_shm).
//
// This is used when buffers can't be shared with the compositor as dmabufs,
// and costs a copy from GPU to CPU memory each frame.
class ShmBuffer : public RenderBuffer {
 public:
  // Allocates a buffer of the given size in pixels.
  //
  // The wl_buffer is created using |shm|, so events for it are dispatched on
  // that object's queue.
  //
  // If |depth_stencil| is true a depth/stencil attachment is added to the
  // framebuffer.
  //
  // Must be called with the render context current.
  static std::unique_ptr<ShmBuffer> Create(EGLManager* egl,
                                           wl_shm* shm,
                                           int width,
                                           int height,
                                           bool depth_stencil);

  ~ShmBuffer() override;

  // |RenderBuffer|
  uint64_t modifier() const override;

  // |RenderBuffer|
  bool PrepareToPresent() override;

 private:
  ShmBuffer(EGLManager* egl, int width, int height);

  bool Initialize(wl_shm* shm, bool depth_stencil);

  GLuint color_ = 0;
  void* data_ = nullptr;
  size_t size_ = 0;

  FML_DISALLOW_COPY_AND_ASSIGN(ShmBuffer);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_SHM_BUFFER_H_
