// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DMABUF_BUFFER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DMABUF_BUFFER_H_

#include <gbm.h>
#include <wayland-client.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/egl_manager.h"
#include "flutter/shell/platform/linux/protocols/linux-dmabuf-v1-client-protocol.h"

namespace flutter {

// A GPU buffer that Flutter renders into and the compositor displays
// directly, without any copies.
//
// The buffer is allocated with GBM, imported into GL as a framebuffer and
// shared with the compositor as a dmabuf.
class DmabufBuffer {
 public:
  // Allocates a buffer of the given size in pixels.
  //
  // |modifiers| are the layouts the compositor supports for the buffer
  // format. If empty, the driver chooses the layout implicitly.
  //
  // The wl_buffer is created using |dmabuf|, so events for it are dispatched
  // on that object's queue.
  //
  // If |depth_stencil| is true a depth/stencil attachment is added to the
  // framebuffer.
  //
  // Must be called with the render context current.
  static std::unique_ptr<DmabufBuffer> Create(
      EGLManager* egl,
      zwp_linux_dmabuf_v1* dmabuf,
      const std::vector<uint64_t>& modifiers,
      int width,
      int height,
      bool depth_stencil);

  // GL objects are only deleted if the render context is current, otherwise
  // they are freed when the context is destroyed.
  ~DmabufBuffer();

  int width() const { return width_; }
  int height() const { return height_; }

  // The GL framebuffer to render into.
  uint32_t framebuffer() const { return framebuffer_; }

  // The Wayland buffer to attach to a surface.
  wl_buffer* buffer() const { return buffer_; }

  // True from when the buffer is committed to a surface until the compositor
  // releases it.
  bool busy() const { return busy_; }
  void set_busy(bool busy) { busy_ = busy; }

  // The DRM format all buffers use.
  static const uint32_t kFormat;

  // The GL format that corresponds to kFormat.
  static const uint32_t kGLFormat;

 private:
  explicit DmabufBuffer(EGLManager* egl);

  bool Initialize(zwp_linux_dmabuf_v1* dmabuf,
                  const std::vector<uint64_t>& modifiers,
                  bool depth_stencil);

  static void BufferRelease(void* data, wl_buffer* buffer);

  static const wl_buffer_listener kBufferListener;

  EGLManager* egl_;
  int width_ = 0;
  int height_ = 0;
  gbm_bo* bo_ = nullptr;
  EGLImageKHR image_ = EGL_NO_IMAGE_KHR;
  GLuint texture_ = 0;
  GLuint depth_stencil_ = 0;
  GLuint framebuffer_ = 0;
  wl_buffer* buffer_ = nullptr;
  bool busy_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(DmabufBuffer);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DMABUF_BUFFER_H_
