// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/gpu_device.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <xf86drm.h>

#include <drm_fourcc.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

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
    InitializeFences(extensions);
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
  if (supports_dmabuf_modifiers_) {
    QueryDmaBufModifiers = reinterpret_cast<PFNEGLQUERYDMABUFMODIFIERSEXTPROC>(
        eglGetProcAddress("eglQueryDmaBufModifiersEXT"));
  }
}

namespace {

// The EGL attributes for each dmabuf plane.
struct PlaneAttributes {
  EGLint fd;
  EGLint offset;
  EGLint pitch;
  EGLint modifier_lo;
  EGLint modifier_hi;
};

constexpr PlaneAttributes kPlaneAttributes[GpuDevice::kMaxDmabufPlanes] = {
    {EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE0_OFFSET_EXT,
     EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
     EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT},
    {EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT,
     EGL_DMA_BUF_PLANE1_PITCH_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
     EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT},
    {EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT,
     EGL_DMA_BUF_PLANE2_PITCH_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
     EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT},
    {EGL_DMA_BUF_PLANE3_FD_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT,
     EGL_DMA_BUF_PLANE3_PITCH_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT,
     EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT},
};

}  // namespace

EGLImageKHR GpuDevice::ImportDmabuf(int width,
                                    int height,
                                    uint32_t format,
                                    uint64_t modifier,
                                    const DmabufPlane* planes,
                                    size_t n_planes,
                                    const EGLint* extra_attributes) {
  if (n_planes < 1 || n_planes > kMaxDmabufPlanes) {
    return EGL_NO_IMAGE_KHR;
  }
  // Without the modifiers extension only the implied layout can be used, of
  // which linear is the only one that is known.
  bool has_modifier = modifier != DRM_FORMAT_MOD_INVALID;
  if (has_modifier && !supports_dmabuf_modifiers_) {
    if (modifier != DRM_FORMAT_MOD_LINEAR) {
      return EGL_NO_IMAGE_KHR;
    }
    has_modifier = false;
  }

  std::vector<EGLint> attributes = {EGL_WIDTH,
                                    width,
                                    EGL_HEIGHT,
                                    height,
                                    EGL_LINUX_DRM_FOURCC_EXT,
                                    static_cast<EGLint>(format)};
  for (size_t i = 0; i < n_planes; i++) {
    const PlaneAttributes& plane = kPlaneAttributes[i];
    attributes.insert(attributes.end(),
                      {plane.fd, planes[i].fd, plane.offset,
                       static_cast<EGLint>(planes[i].offset), plane.pitch,
                       static_cast<EGLint>(planes[i].stride)});
    if (has_modifier) {
      attributes.insert(
          attributes.end(),
          {plane.modifier_lo, static_cast<EGLint>(modifier & 0xffffffff),
           plane.modifier_hi, static_cast<EGLint>(modifier >> 32)});
    }
  }
  for (const EGLint* attribute = extra_attributes;
       attribute != nullptr && *attribute != EGL_NONE; attribute += 2) {
    attributes.insert(attributes.end(), {attribute[0], attribute[1]});
  }
  attributes.push_back(EGL_NONE);

  return CreateImage(display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr,
                     attributes.data());
}

bool GpuDevice::IsExternalOnly(uint32_t format, uint64_t modifier) {
  // Without the modifiers extension EGL can't say, so assume only RGB formats
  // can be sampled as 2D textures.
  auto is_rgb = [format]() {
    switch (format) {
      case DRM_FORMAT_ARGB8888:
      case DRM_FORMAT_XRGB8888:
      case DRM_FORMAT_ABGR8888:
      case DRM_FORMAT_XBGR8888:
      case DRM_FORMAT_RGBA8888:
      case DRM_FORMAT_RGBX8888:
      case DRM_FORMAT_BGRA8888:
      case DRM_FORMAT_BGRX8888:
      case DRM_FORMAT_ARGB2101010:
      case DRM_FORMAT_XRGB2101010:
      case DRM_FORMAT_ABGR2101010:
      case DRM_FORMAT_XBGR2101010:
      case DRM_FORMAT_RGB565:
      case DRM_FORMAT_BGR565:
      case DRM_FORMAT_ABGR16161616F:
      case DRM_FORMAT_XBGR16161616F:
        return true;
      default:
        return false;
    }
  };
  if (QueryDmaBufModifiers == nullptr || modifier == DRM_FORMAT_MOD_INVALID) {
    return !is_rgb();
  }

  std::scoped_lock lock(external_only_mutex_);
  auto key = std::make_pair(format, modifier);
  auto it = external_only_.find(key);
  if (it != external_only_.end()) {
    return it->second;
  }

  bool external_only = !is_rgb();
  EGLint count = 0;
  if (QueryDmaBufModifiers(display_, static_cast<EGLint>(format), 0, nullptr,
                           nullptr, &count) &&
      count > 0) {
    std::vector<EGLuint64KHR> modifiers(count);
    std::vector<EGLBoolean> external(count);
    if (QueryDmaBufModifiers(display_, static_cast<EGLint>(format), count,
                             modifiers.data(), external.data(), &count)) {
      for (EGLint i = 0; i < count; i++) {
        if (modifiers[i] == modifier) {
          external_only = external[i] == EGL_TRUE;
          break;
        }
      }
    }
  }
  external_only_[key] = external_only;
  return external_only;
}

std::vector<uint64_t> GpuDevice::GetRenderModifiers(uint32_t format) {
  if (QueryDmaBufModifiers == nullptr) {
    return {};
  }
  EGLint count = 0;
  if (!QueryDmaBufModifiers(display_, static_cast<EGLint>(format), 0, nullptr,
                            nullptr, &count) ||
      count <= 0) {
    return {};
  }
  std::vector<EGLuint64KHR> modifiers(count);
  std::vector<EGLBoolean> external(count);
  if (!QueryDmaBufModifiers(display_, static_cast<EGLint>(format), count,
                            modifiers.data(), external.data(), &count)) {
    return {};
  }
  std::vector<uint64_t> result;
  for (EGLint i = 0; i < count; i++) {
    if (external[i] != EGL_TRUE) {
      result.push_back(modifiers[i]);
    }
  }
  return result;
}

void GpuDevice::InitializeFences(const char* extensions) {
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
  if (CreateSync == nullptr || DestroySync == nullptr ||
      DupNativeFenceFD == nullptr) {
    CreateSync = nullptr;
    DestroySync = nullptr;
    DupNativeFenceFD = nullptr;
    return;
  }
  if (HasExtension(extensions, "EGL_KHR_wait_sync")) {
    WaitSync = reinterpret_cast<PFNEGLWAITSYNCKHRPROC>(
        eglGetProcAddress("eglWaitSyncKHR"));
  }

  uint64_t timeline_supported = 0;
  supports_explicit_sync_ = drmGetCap(drm_fd_.get(), DRM_CAP_SYNCOBJ_TIMELINE,
                                      &timeline_supported) == 0 &&
                            timeline_supported != 0;
}

}  // namespace flutter
