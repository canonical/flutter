// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_DMABUF_BUFFER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_DMABUF_BUFFER_H_

#include <gbm.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/render_buffer.h"
#include "flutter/shell/platform/linux/wayland/protocols/linux-dmabuf-v1-client-protocol.h"
#include "flutter/shell/platform/linux/wayland/protocols/wayland-client-protocol.h"

namespace flutter {

// A GPU buffer that Flutter renders into and the compositor displays
// directly, without any copies.
//
// The buffer is allocated with GBM, imported into GL as a framebuffer and
// shared with the compositor as a dmabuf.
class DmabufBuffer : public RenderBuffer {
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

  ~DmabufBuffer() override;

  // |RenderBuffer|
  // DRM_FORMAT_MOD_INVALID if the driver chose the layout implicitly.
  uint64_t modifier() const override { return modifier_; }

  // |RenderBuffer|
  bool PrepareToPresent() override { return true; }

  // The DRM format all buffers use.
  static const uint32_t kFormat;

 private:
  DmabufBuffer(EGLManager* egl, int width, int height);

  bool Initialize(zwp_linux_dmabuf_v1* dmabuf,
                  const std::vector<uint64_t>& modifiers,
                  bool depth_stencil);

  gbm_bo* bo_ = nullptr;
  uint64_t modifier_ = 0;
  EGLImageKHR image_ = EGL_NO_IMAGE_KHR;
  GLuint texture_ = 0;

  FML_DISALLOW_COPY_AND_ASSIGN(DmabufBuffer);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_DMABUF_BUFFER_H_
