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
#include <string>

#include "flutter/fml/macros.h"
#include "flutter/fml/unique_fd.h"

namespace flutter {

// The GPU that Flutter renders with, shared by all engines.
//
// Rendering is done without EGL surfaces: Flutter draws into framebuffers
// backed by GBM buffers that are shared with the compositor as dmabufs, or
// copied into shared memory if that is not possible.
//
// This may be used from any thread.
class GpuDevice {
 public:
  // Opens a render node and initializes EGL on it. If |device| is set, the
  // render node for that device is used, otherwise the first available render
  // node is used. Returns nullptr on failure.
  static std::unique_ptr<GpuDevice> Create(std::optional<dev_t> device);

  ~GpuDevice();

  int drm_fd() const { return drm_fd_.get(); }

  // The path of the render node, e.g. "/dev/dri/renderD128".
  const std::string& path() const { return path_; }
  gbm_device* gbm() const { return gbm_; }
  EGLDisplay display() const { return display_; }

  // True if the EGL implementation can import dmabufs.
  bool supports_dmabuf_import() const { return supports_dmabuf_import_; }

  // True if the EGL implementation can import dmabufs with explicit
  // modifiers.
  bool supports_dmabuf_modifiers() const { return supports_dmabuf_modifiers_; }

  // True if the GPU can produce and consume fences for explicit
  // synchronization with the compositor, using DRM timeline syncobjs and EGL
  // native fences.
  bool supports_explicit_sync() const { return supports_explicit_sync_; }

  // Extension functions used to import dmabufs. Only set if
  // supports_dmabuf_import() is true.
  PFNEGLCREATEIMAGEKHRPROC CreateImage = nullptr;
  PFNEGLDESTROYIMAGEKHRPROC DestroyImage = nullptr;
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC ImageTargetTexture2D = nullptr;

  // Extension functions used for explicit synchronization. Only set if
  // supports_explicit_sync() is true. WaitSync may be nullptr if the GPU
  // can't wait for fences, in which case they must be waited for on the CPU.
  PFNEGLCREATESYNCKHRPROC CreateSync = nullptr;
  PFNEGLDESTROYSYNCKHRPROC DestroySync = nullptr;
  PFNEGLDUPNATIVEFENCEFDANDROIDPROC DupNativeFenceFD = nullptr;
  PFNEGLWAITSYNCKHRPROC WaitSync = nullptr;

 private:
  GpuDevice() = default;

  bool Initialize(std::optional<dev_t> device);

  // Checks for and loads the functions used to import dmabufs.
  void InitializeDmabufImport(const char* extensions);

  // Checks for and loads the functions used for explicit synchronization.
  void InitializeExplicitSync(const char* extensions);

  std::string path_;
  fml::UniqueFD drm_fd_;
  gbm_device* gbm_ = nullptr;
  EGLDisplay display_ = EGL_NO_DISPLAY;
  bool supports_dmabuf_import_ = false;
  bool supports_dmabuf_modifiers_ = false;
  bool supports_explicit_sync_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(GpuDevice);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_GPU_DEVICE_H_
