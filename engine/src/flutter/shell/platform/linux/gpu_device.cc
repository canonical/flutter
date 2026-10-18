// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/gpu_device.h"

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

}  // namespace

std::unique_ptr<GpuDevice> GpuDevice::Create(std::optional<dev_t> device) {
  std::unique_ptr<GpuDevice> gpu(new GpuDevice());
  if (!gpu->Initialize(device)) {
    return nullptr;
  }
  return gpu;
}

GpuDevice::~GpuDevice() {
  if (display_ != EGL_NO_DISPLAY) {
    eglTerminate(display_);
  }
  if (gbm_ != nullptr) {
    gbm_device_destroy(gbm_);
  }
}

bool GpuDevice::Initialize(std::optional<dev_t> device) {
  path_ = FindRenderNode(device);
  const std::string& path = path_;
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
       {"EGL_KHR_surfaceless_context", "EGL_KHR_no_config_context"}) {
    if (!HasExtension(extensions, required)) {
      FML_LOG(ERROR) << "EGL implementation does not support " << required;
      return false;
    }
  }

  InitializeDmabufImport(extensions);
  if (supports_dmabuf_import_) {
    InitializeExplicitSync(extensions);
  }

  FML_DLOG(INFO) << "Rendering with " << path;
  return true;
}

void GpuDevice::InitializeDmabufImport(const char* extensions) {
  if (!HasExtension(extensions, "EGL_EXT_image_dma_buf_import")) {
    return;
  }
  CreateImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
      eglGetProcAddress("eglCreateImageKHR"));
  DestroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
      eglGetProcAddress("eglDestroyImageKHR"));
  ImageTargetTexture2D = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
      eglGetProcAddress("glEGLImageTargetTexture2DOES"));
  supports_dmabuf_import_ = CreateImage != nullptr && DestroyImage != nullptr &&
                            ImageTargetTexture2D != nullptr;
  supports_dmabuf_modifiers_ =
      supports_dmabuf_import_ &&
      HasExtension(extensions, "EGL_EXT_image_dma_buf_import_modifiers");
}

void GpuDevice::InitializeExplicitSync(const char* extensions) {
  uint64_t timeline_supported = 0;
  if (drmGetCap(drm_fd_.get(), DRM_CAP_SYNCOBJ_TIMELINE, &timeline_supported) !=
          0 ||
      timeline_supported == 0) {
    return;
  }
  if (!HasExtension(extensions, "EGL_KHR_fence_sync") ||
      !HasExtension(extensions, "EGL_ANDROID_native_fence_sync")) {
    return;
  }

  CreateSync = reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(
      eglGetProcAddress("eglCreateSyncKHR"));
  DestroySync = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(
      eglGetProcAddress("eglDestroySyncKHR"));
  DupNativeFenceFD = reinterpret_cast<PFNEGLDUPNATIVEFENCEFDANDROIDPROC>(
      eglGetProcAddress("eglDupNativeFenceFDANDROID"));
  if (HasExtension(extensions, "EGL_KHR_wait_sync")) {
    WaitSync = reinterpret_cast<PFNEGLWAITSYNCKHRPROC>(
        eglGetProcAddress("eglWaitSyncKHR"));
  }
  supports_explicit_sync_ = CreateSync != nullptr && DestroySync != nullptr &&
                            DupNativeFenceFD != nullptr;
}

}  // namespace flutter
