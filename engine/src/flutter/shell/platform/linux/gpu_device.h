// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_GPU_DEVICE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_GPU_DEVICE_H_

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

// The GPU that Flutter renders with, shared by all engines.
//
// Rendering is done without EGL surfaces: Flutter draws into framebuffers
// backed by GBM buffers that are shared with the compositor as dmabufs.
//
// This may be used from any thread.
class GpuDevice {
 public:
  // Opens a render node and initializes EGL on it. If |device| is set, the
  // render node for that device is used, otherwise the first available render
  // node is used. Returns nullptr on failure.
  static std::unique_ptr<GpuDevice> Create(std::optional<dev_t> device);

  ~GpuDevice();

  gbm_device* gbm() const { return gbm_; }
  EGLDisplay display() const { return display_; }

  // True if the EGL implementation can import dmabufs with explicit
  // modifiers.
  bool supports_dmabuf_modifiers() const { return supports_dmabuf_modifiers_; }

  // Extension functions used to import dmabufs.
  PFNEGLCREATEIMAGEKHRPROC CreateImage = nullptr;
  PFNEGLDESTROYIMAGEKHRPROC DestroyImage = nullptr;
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC ImageTargetTexture2D = nullptr;

 private:
  GpuDevice() = default;

  bool Initialize(std::optional<dev_t> device);

  fml::UniqueFD drm_fd_;
  gbm_device* gbm_ = nullptr;
  EGLDisplay display_ = EGL_NO_DISPLAY;
  bool supports_dmabuf_modifiers_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(GpuDevice);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_GPU_DEVICE_H_
