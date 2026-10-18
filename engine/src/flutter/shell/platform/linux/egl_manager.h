// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_EGL_MANAGER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_EGL_MANAGER_H_

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#include <sys/types.h>

#include <memory>
#include <optional>

#include "flutter/fml/macros.h"
#include "flutter/fml/unique_fd.h"

namespace flutter {

// Manages the GPU device and the EGL contexts Flutter renders with.
//
// Rendering is done without EGL surfaces: Flutter draws into framebuffers
// backed by GBM buffers that are shared with the compositor as dmabufs.
class EGLManager {
 public:
  // Opens a render node and creates the EGL contexts. If |device| is set,
  // the render node for that device is used, otherwise the first available
  // render node is used. Returns nullptr on failure.
  static std::unique_ptr<EGLManager> Create(std::optional<dev_t> device);

  ~EGLManager();

  gbm_device* gbm() const { return gbm_; }
  EGLDisplay display() const { return display_; }

  // Makes the render context current on the calling thread.
  bool MakeRenderContextCurrent();

  // Makes the resource upload context current on the calling thread.
  bool MakeResourceContextCurrent();

  // Releases any context on the calling thread.
  bool ClearCurrent();

  // Returns true if the render context is current on the calling thread.
  bool IsRenderContextCurrent() const;

  // Extension functions used to import dmabufs.
  PFNEGLCREATEIMAGEKHRPROC CreateImage = nullptr;
  PFNEGLDESTROYIMAGEKHRPROC DestroyImage = nullptr;
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC ImageTargetTexture2D = nullptr;

  // True if the EGL implementation can import dmabufs with explicit
  // modifiers.
  bool supports_dmabuf_modifiers() const { return supports_dmabuf_modifiers_; }

 private:
  EGLManager() = default;

  bool Initialize(std::optional<dev_t> device);

  fml::UniqueFD drm_fd_;
  gbm_device* gbm_ = nullptr;
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLContext render_context_ = EGL_NO_CONTEXT;
  EGLContext resource_context_ = EGL_NO_CONTEXT;
  bool supports_dmabuf_modifiers_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(EGLManager);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_EGL_MANAGER_H_
