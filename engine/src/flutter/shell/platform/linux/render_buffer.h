// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_RENDER_BUFFER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_RENDER_BUFFER_H_

#include <atomic>
#include <cstdint>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/egl_manager.h"
#include "flutter/shell/platform/linux/wayland/protocols/wayland-client-protocol.h"

namespace flutter {

// A buffer that Flutter renders into with OpenGL and that is attached to a
// Wayland surface.
class RenderBuffer {
 public:
  // GL objects are only deleted if the render context is current, otherwise
  // they are freed when the context is destroyed.
  virtual ~RenderBuffer();

  int width() const { return width_; }
  int height() const { return height_; }

  // The GL framebuffer to render into.
  uint32_t framebuffer() const { return framebuffer_; }

  // The DRM format modifier describing the buffer layout, or
  // DRM_FORMAT_MOD_INVALID if the layout is not known.
  virtual uint64_t modifier() const = 0;

  // Makes the rendered contents available to the compositor. Called on the
  // raster thread with the render context current, before the buffer is
  // attached to a surface.
  virtual bool PrepareToPresent() = 0;

  // The Wayland buffer to attach to a surface.
  wl_buffer* buffer() const { return buffer_; }

  // True from when the buffer is committed to a surface until the compositor
  // releases it.
  bool busy() const { return busy_; }
  void set_busy(bool busy) { busy_ = busy; }

  // The GL format of the color attachment.
  static const uint32_t kGLFormat;

 protected:
  RenderBuffer(EGLManager* egl, int width, int height);

  // Creates the framebuffer with |color| as its color attachment, where
  // |color_target| is GL_TEXTURE_2D or GL_RENDERBUFFER. If |depth_stencil| is
  // true a depth/stencil attachment is added.
  bool CreateFramebuffer(GLenum color_target, GLuint color, bool depth_stencil);

  // Sets the Wayland buffer, which this object takes ownership of.
  void SetBuffer(wl_buffer* buffer);

  EGLManager* egl() const { return egl_; }

 private:
  static void BufferRelease(void* data, wl_buffer* buffer);

  static const wl_buffer_listener kBufferListener;

  EGLManager* egl_;
  int width_;
  int height_;
  GLuint depth_stencil_ = 0;
  GLuint framebuffer_ = 0;
  wl_buffer* buffer_ = nullptr;
  std::atomic<bool> busy_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(RenderBuffer);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_RENDER_BUFFER_H_
