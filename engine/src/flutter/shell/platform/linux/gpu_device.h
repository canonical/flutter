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

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

  // A plane of a dmabuf.
  struct DmabufPlane {
    int fd;
    uint32_t offset;
    uint32_t stride;
  };

  // Imports a dmabuf as an EGLImage, which must be destroyed with
  // DestroyImage. |modifier| is DRM_FORMAT_MOD_INVALID if the layout is
  // implied by the driver. |extra_attributes|, which may be nullptr, are
  // EGL_NONE terminated attributes added to the import. Returns
  // EGL_NO_IMAGE_KHR on failure. Must only be called if
  // supports_dmabuf_import() is true.
  EGLImageKHR ImportDmabuf(int width,
                           int height,
                           uint32_t format,
                           uint64_t modifier,
                           const DmabufPlane* planes,
                           size_t n_planes,
                           const EGLint* extra_attributes = nullptr);

  // The maximum number of planes a dmabuf can have.
  static constexpr size_t kMaxDmabufPlanes = 4;

  // Returns true if dmabufs with |format| (a DRM fourcc) and |modifier| can
  // only be sampled as GL_TEXTURE_EXTERNAL_OES, e.g. YUV formats. May be
  // called from any thread.
  bool IsExternalOnly(uint32_t format, uint64_t modifier);

  // Returns the modifiers with which dmabufs of |format| can be rendered
  // into with EGL. Empty if modifiers aren't supported.
  std::vector<uint64_t> GetRenderModifiers(uint32_t format);

  // True if the GPU can produce and consume fences for explicit
  // synchronization with the compositor, using DRM timeline syncobjs and EGL
  // native fences.
  bool supports_explicit_sync() const { return supports_explicit_sync_; }

  // Extension functions used to import dmabufs. Only set if
  // supports_dmabuf_import() is true.
  PFNEGLCREATEIMAGEKHRPROC CreateImage = nullptr;
  PFNEGLDESTROYIMAGEKHRPROC DestroyImage = nullptr;
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC ImageTargetTexture2D = nullptr;
  PFNEGLQUERYDMABUFMODIFIERSEXTPROC QueryDmaBufModifiers = nullptr;

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

  // Whether each format and modifier is external only, as reported by EGL.
  std::mutex external_only_mutex_;
  std::map<std::pair<uint32_t, uint64_t>, bool> external_only_;

  FML_DISALLOW_COPY_AND_ASSIGN(GpuDevice);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_GPU_DEVICE_H_
