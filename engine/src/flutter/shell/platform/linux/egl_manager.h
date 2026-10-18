// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_EGL_MANAGER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_EGL_MANAGER_H_

#include <EGL/egl.h>

#include <memory>
#include <string>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/gpu_device.h"

namespace flutter {

// Manages the EGL contexts an engine renders with.
//
// Each engine has its own contexts as each engine renders on its own raster
// thread.
class EGLManager {
 public:
  // Creates the contexts on |gpu|. Returns nullptr on failure.
  static std::unique_ptr<EGLManager> Create(GpuDevice* gpu);

  ~EGLManager();

  GpuDevice* gpu() const { return gpu_; }

  // Information about the OpenGL implementation, from glGetString.
  const std::string& gl_vendor() const { return gl_vendor_; }
  const std::string& gl_renderer() const { return gl_renderer_; }
  const std::string& gl_version() const { return gl_version_; }

  // True if pixels can be read in BGRA order (GL_EXT_read_format_bgra).
  bool supports_read_bgra() const { return supports_read_bgra_; }

  // Makes the render context current on the calling thread.
  bool MakeRenderContextCurrent();

  // Makes the resource upload context current on the calling thread.
  bool MakeResourceContextCurrent();

  // Releases any context on the calling thread.
  bool ClearCurrent();

  // Returns true if the render context is current on the calling thread.
  bool IsRenderContextCurrent() const;

 private:
  explicit EGLManager(GpuDevice* gpu);

  bool Initialize();

  // Reads the OpenGL implementation strings and extensions using the render
  // context.
  void QueryGLStrings();

  GpuDevice* gpu_;
  EGLContext render_context_ = EGL_NO_CONTEXT;
  EGLContext resource_context_ = EGL_NO_CONTEXT;
  std::string gl_vendor_;
  std::string gl_renderer_;
  std::string gl_version_;
  bool supports_read_bgra_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(EGLManager);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_EGL_MANAGER_H_
