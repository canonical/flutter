// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/egl_manager.h"

#include <cstdio>

#include "flutter/fml/logging.h"

namespace flutter {

namespace {

EGLContext CreateContext(EGLDisplay display, EGLContext share_context) {
  for (EGLint major_version : {3, 2}) {
    const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION, major_version,
                                 EGL_NONE};
    EGLContext context =
        eglCreateContext(display, EGL_NO_CONFIG_KHR, share_context, attributes);
    if (context != EGL_NO_CONTEXT) {
      return context;
    }
  }
  return EGL_NO_CONTEXT;
}

}  // namespace

std::unique_ptr<EGLManager> EGLManager::Create(GpuDevice* gpu) {
  std::unique_ptr<EGLManager> manager(new EGLManager(gpu));
  if (!manager->Initialize()) {
    return nullptr;
  }
  return manager;
}

EGLManager::EGLManager(GpuDevice* gpu) : gpu_(gpu) {}

EGLManager::~EGLManager() {
  EGLDisplay display = gpu_->display();
  if (eglGetCurrentContext() == render_context_ ||
      eglGetCurrentContext() == resource_context_) {
    ClearCurrent();
  }
  if (resource_context_ != EGL_NO_CONTEXT) {
    eglDestroyContext(display, resource_context_);
  }
  if (render_context_ != EGL_NO_CONTEXT) {
    eglDestroyContext(display, render_context_);
  }
}

bool EGLManager::Initialize() {
  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    FML_LOG(ERROR) << "Failed to bind OpenGL ES API";
    return false;
  }

  render_context_ = CreateContext(gpu_->display(), EGL_NO_CONTEXT);
  if (render_context_ == EGL_NO_CONTEXT) {
    FML_LOG(ERROR) << "Failed to create EGL context: 0x" << std::hex
                   << eglGetError();
    return false;
  }
  resource_context_ = CreateContext(gpu_->display(), render_context_);
  if (resource_context_ == EGL_NO_CONTEXT) {
    FML_LOG(ERROR) << "Failed to create EGL resource context: 0x" << std::hex
                   << eglGetError();
    return false;
  }
  QueryGLStrings();
  return true;
}

void EGLManager::QueryGLStrings() {
  EGLDisplay previous_display = eglGetCurrentDisplay();
  EGLContext previous_context = eglGetCurrentContext();
  EGLSurface previous_draw = eglGetCurrentSurface(EGL_DRAW);
  EGLSurface previous_read = eglGetCurrentSurface(EGL_READ);
  if (!MakeRenderContextCurrent()) {
    return;
  }
  auto get_string = [](GLenum name) {
    const GLubyte* value = glGetString(name);
    return value != nullptr ? std::string(reinterpret_cast<const char*>(value))
                            : std::string();
  };
  gl_vendor_ = get_string(GL_VENDOR);
  gl_renderer_ = get_string(GL_RENDERER);
  gl_version_ = get_string(GL_VERSION);
  std::string extensions = " " + get_string(GL_EXTENSIONS) + " ";
  auto has_extension = [&extensions](const char* name) {
    return extensions.find(" " + std::string(name) + " ") != std::string::npos;
  };
  supports_read_bgra_ = has_extension("GL_EXT_read_format_bgra");
  supports_external_textures_ = has_extension("GL_OES_EGL_image_external");
  // The version string is "OpenGL ES <major>.<minor> <vendor information>".
  int major_version = 0;
  sscanf(gl_version_.c_str(), "OpenGL ES %d", &major_version);
  supports_unpack_row_length_ =
      major_version >= 3 || has_extension("GL_EXT_unpack_subimage");
  if (previous_context != EGL_NO_CONTEXT) {
    eglMakeCurrent(previous_display, previous_draw, previous_read,
                   previous_context);
  } else {
    ClearCurrent();
  }
}

bool EGLManager::MakeRenderContextCurrent() {
  return eglMakeCurrent(gpu_->display(), EGL_NO_SURFACE, EGL_NO_SURFACE,
                        render_context_) == EGL_TRUE;
}

bool EGLManager::MakeResourceContextCurrent() {
  return eglMakeCurrent(gpu_->display(), EGL_NO_SURFACE, EGL_NO_SURFACE,
                        resource_context_) == EGL_TRUE;
}

bool EGLManager::ClearCurrent() {
  return eglMakeCurrent(gpu_->display(), EGL_NO_SURFACE, EGL_NO_SURFACE,
                        EGL_NO_CONTEXT) == EGL_TRUE;
}

bool EGLManager::IsRenderContextCurrent() const {
  return eglGetCurrentContext() == render_context_;
}

bool EGLManager::IsContextCurrent() const {
  EGLContext context = eglGetCurrentContext();
  return context == render_context_ || context == resource_context_;
}

}  // namespace flutter
