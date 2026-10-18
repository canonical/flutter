// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/egl_manager.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <xf86drm.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "flutter/fml/logging.h"

namespace flutter {

namespace {

constexpr int kMaxDevices = 64;

bool HasExtension(const char* extensions, const char* name) {
  if (extensions == nullptr) {
    return false;
  }
  size_t name_length = strlen(name);
  const char* start = extensions;
  while (true) {
    const char* match = strstr(start, name);
    if (match == nullptr) {
      return false;
    }
    char end = match[name_length];
    if ((match == extensions || match[-1] == ' ') &&
        (end == ' ' || end == '\0')) {
      return true;
    }
    start = match + name_length;
  }
}

// Returns true if |path| is the device node for |device|.
bool NodeMatchesDevice(const char* path, dev_t device) {
  struct stat info;
  return stat(path, &info) == 0 && info.st_rdev == device;
}

// Returns the path to the render node to use.
std::string FindRenderNode(std::optional<dev_t> device) {
  drmDevicePtr devices[kMaxDevices];
  int n_devices = drmGetDevices2(0, devices, kMaxDevices);
  if (n_devices < 0) {
    return "";
  }

  std::string first_render_node;
  std::string matching_render_node;
  for (int i = 0; i < n_devices; i++) {
    drmDevicePtr d = devices[i];
    if ((d->available_nodes & (1 << DRM_NODE_RENDER)) == 0) {
      continue;
    }
    const char* render_node = d->nodes[DRM_NODE_RENDER];
    if (first_render_node.empty()) {
      first_render_node = render_node;
    }
    if (!device.has_value() || !matching_render_node.empty()) {
      continue;
    }
    // The compositor may report either the primary or render node.
    for (int type : {DRM_NODE_PRIMARY, DRM_NODE_RENDER}) {
      if ((d->available_nodes & (1 << type)) != 0 &&
          NodeMatchesDevice(d->nodes[type], *device)) {
        matching_render_node = render_node;
        break;
      }
    }
  }
  drmFreeDevices(devices, n_devices);

  if (!matching_render_node.empty()) {
    return matching_render_node;
  }
  if (device.has_value()) {
    FML_LOG(WARNING) << "Unable to find render node for compositor device, "
                        "using "
                     << first_render_node;
  }
  return first_render_node;
}

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

std::unique_ptr<EGLManager> EGLManager::Create(std::optional<dev_t> device) {
  std::unique_ptr<EGLManager> manager(new EGLManager());
  if (!manager->Initialize(device)) {
    return nullptr;
  }
  return manager;
}

EGLManager::~EGLManager() {
  if (display_ != EGL_NO_DISPLAY) {
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (resource_context_ != EGL_NO_CONTEXT) {
      eglDestroyContext(display_, resource_context_);
    }
    if (render_context_ != EGL_NO_CONTEXT) {
      eglDestroyContext(display_, render_context_);
    }
    eglTerminate(display_);
  }
  if (gbm_ != nullptr) {
    gbm_device_destroy(gbm_);
  }
}

bool EGLManager::Initialize(std::optional<dev_t> device) {
  std::string path = FindRenderNode(device);
  if (path.empty()) {
    FML_LOG(ERROR) << "No GPU render node available";
    return false;
  }
  drm_fd_.reset(open(path.c_str(), O_RDWR | O_CLOEXEC));
  if (!drm_fd_.is_valid()) {
    FML_LOG(ERROR) << "Failed to open " << path << ": " << strerror(errno);
    return false;
  }

  gbm_ = gbm_create_device(drm_fd_.get());
  if (gbm_ == nullptr) {
    FML_LOG(ERROR) << "Failed to create GBM device for " << path;
    return false;
  }

  display_ = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm_, nullptr);
  if (display_ == EGL_NO_DISPLAY) {
    FML_LOG(ERROR) << "Failed to get EGL display for " << path;
    return false;
  }
  if (!eglInitialize(display_, nullptr, nullptr)) {
    FML_LOG(ERROR) << "Failed to initialize EGL";
    display_ = EGL_NO_DISPLAY;
    return false;
  }

  const char* extensions = eglQueryString(display_, EGL_EXTENSIONS);
  for (const char* required :
       {"EGL_KHR_surfaceless_context", "EGL_KHR_no_config_context",
        "EGL_EXT_image_dma_buf_import"}) {
    if (!HasExtension(extensions, required)) {
      FML_LOG(ERROR) << "EGL implementation does not support " << required;
      return false;
    }
  }
  supports_dmabuf_modifiers_ =
      HasExtension(extensions, "EGL_EXT_image_dma_buf_import_modifiers");

  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    FML_LOG(ERROR) << "Failed to bind OpenGL ES API";
    return false;
  }

  render_context_ = CreateContext(display_, EGL_NO_CONTEXT);
  if (render_context_ == EGL_NO_CONTEXT) {
    FML_LOG(ERROR) << "Failed to create EGL context: 0x" << std::hex
                   << eglGetError();
    return false;
  }
  resource_context_ = CreateContext(display_, render_context_);
  if (resource_context_ == EGL_NO_CONTEXT) {
    FML_LOG(ERROR) << "Failed to create EGL resource context: 0x" << std::hex
                   << eglGetError();
    return false;
  }

  CreateImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
      eglGetProcAddress("eglCreateImageKHR"));
  DestroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
      eglGetProcAddress("eglDestroyImageKHR"));
  ImageTargetTexture2D = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
      eglGetProcAddress("glEGLImageTargetTexture2DOES"));
  if (CreateImage == nullptr || DestroyImage == nullptr ||
      ImageTargetTexture2D == nullptr) {
    FML_LOG(ERROR) << "EGL implementation does not support EGL images";
    return false;
  }

  FML_DLOG(INFO) << "Rendering with " << path;
  return true;
}

bool EGLManager::MakeRenderContextCurrent() {
  return eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        render_context_) == EGL_TRUE;
}

bool EGLManager::MakeResourceContextCurrent() {
  return eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        resource_context_) == EGL_TRUE;
}

bool EGLManager::ClearCurrent() {
  return eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        EGL_NO_CONTEXT) == EGL_TRUE;
}

bool EGLManager::IsRenderContextCurrent() const {
  return eglGetCurrentContext() == render_context_;
}

}  // namespace flutter
