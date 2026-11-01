// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/kms/kms_platform_view.h"

#include <GLES2/gl2ext.h>
#include <fcntl.h>

#include <cmath>
#include <vector>

#include "flutter/fml/logging.h"
#include "flutter/fml/unique_fd.h"
#include "flutter/shell/platform/linux/egl_manager.h"
#include "flutter/shell/platform/linux/gpu_device.h"

namespace flutter {

// A buffer imported into the GPU when it is first drawn.
class KmsPlatformView::KmsBuffer : public LinuxPlatformView::Buffer {
 public:
  KmsBuffer(std::shared_ptr<KmsPlatformView> view,
            const DmabufFrame& frame,
            std::vector<fml::UniqueFD> fds)
      : Buffer(std::move(view), frame), frame_(frame), fds_(std::move(fds)) {
    frame_.release = nullptr;
    for (size_t i = 0; i < fds_.size(); i++) {
      frame_.planes[i].fd = fds_[i].get();
    }
  }

  ~KmsBuffer() override {
    Remove();
    // The GPU keeps the memory until it has finished any drawing using it.
    if (image_ != EGL_NO_IMAGE_KHR) {
      gpu_->DestroyImage(gpu_->display(), image_);
    }
  }

  GLenum target() const { return target_; }
  EGLImageKHR image() const { return image_; }

  // Imports the buffer into the GPU used by |egl|, if not done already.
  // Called with the view's mutex held.
  bool Import(EGLManager* egl) {
    if (image_ != EGL_NO_IMAGE_KHR || import_failed_) {
      return !import_failed_;
    }
    import_failed_ = true;
    GpuDevice* gpu = egl->gpu();
    if (!gpu->supports_dmabuf_import()) {
      FML_LOG(ERROR) << "Unable to show platform view, the GPU doesn't "
                        "support importing dmabufs";
      return false;
    }
    // Formats the GPU can't sample directly (e.g. YUV) are sampled as
    // external textures, which convert them to RGB as they are read.
    target_ = gpu->IsExternalOnly(frame_.format, frame_.modifier)
                  ? GL_TEXTURE_EXTERNAL_OES
                  : GL_TEXTURE_2D;
    if (target_ == GL_TEXTURE_EXTERNAL_OES &&
        !egl->supports_external_textures()) {
      FML_LOG(ERROR) << "Unable to show platform view with format 0x"
                     << std::hex << frame_.format
                     << ", GL_OES_EGL_image_external is not supported";
      return false;
    }
    std::vector<GpuDevice::DmabufPlane> planes;
    for (const DmabufFrame::Plane& plane : frame_.planes) {
      planes.push_back({plane.fd, plane.offset, plane.stride});
    }
    std::vector<EGLint> yuv_attributes = GetYuvAttributes(frame_);
    image_ = gpu->ImportDmabuf(frame_.width, frame_.height, frame_.format,
                               frame_.modifier, planes.data(), planes.size(),
                               yuv_attributes.data());
    if (image_ == EGL_NO_IMAGE_KHR) {
      FML_LOG(ERROR) << "Failed to import platform view buffer with format 0x"
                     << std::hex << frame_.format << " and modifier 0x"
                     << frame_.modifier << ": 0x" << eglGetError();
      return false;
    }
    gpu_ = gpu;
    import_failed_ = false;
    return true;
  }

 private:
  DmabufFrame frame_;
  std::vector<fml::UniqueFD> fds_;
  GpuDevice* gpu_ = nullptr;
  EGLImageKHR image_ = EGL_NO_IMAGE_KHR;
  GLenum target_ = GL_TEXTURE_2D;
  bool import_failed_ = false;
};

KmsPlatformView::KmsPlatformView(int64_t id,
                                 EventLoop* event_loop,
                                 std::function<void()> redraw)
    : LinuxPlatformView(id, event_loop), redraw_(std::move(redraw)) {}

KmsPlatformView::~KmsPlatformView() = default;

std::unique_ptr<LinuxPlatformView::Buffer> KmsPlatformView::CreateBackendBuffer(
    const DmabufFrame& frame) {
  // The buffer is imported when it is drawn on the raster thread, so it
  // keeps its own references to the planes.
  std::vector<fml::UniqueFD> fds;
  for (const DmabufFrame::Plane& plane : frame.planes) {
    fml::UniqueFD fd(fcntl(plane.fd, F_DUPFD_CLOEXEC, 0));
    if (!fd.is_valid()) {
      FML_LOG(ERROR) << "Invalid platform view buffer file descriptor";
      return nullptr;
    }
    fds.push_back(std::move(fd));
  }
  return std::make_unique<KmsBuffer>(
      std::static_pointer_cast<KmsPlatformView>(shared_from_this()), frame,
      std::move(fds));
}

void KmsPlatformView::OnPresent() {
  if (redraw_) {
    redraw_();
  }
}

uint64_t KmsPlatformView::Draw(
    EGLManager* egl,
    const PlatformViewGeometry& geometry,
    double scale,
    const std::function<bool(GLenum target, GLuint texture)>& draw) {
  std::scoped_lock lock(mutex_);
  if (disposed_) {
    return 0;
  }
  if (geometry.approximated && !warned_approximated_) {
    FML_LOG(WARNING) << "Platform view " << id()
                     << " is rotated, skewed or has a rounded clip, which "
                        "can't be shown exactly";
    warned_approximated_ = true;
  }
  ReportSize(static_cast<int>(std::round(geometry.rect.width)),
             static_cast<int>(std::round(geometry.rect.height)), scale);

  auto* buffer = static_cast<KmsBuffer*>(current_);
  if (buffer == nullptr || !buffer->Import(egl)) {
    return 0;
  }

  GLenum target = buffer->target();
  GLint saved_texture = 0;
  glGetIntegerv(target == GL_TEXTURE_2D ? GL_TEXTURE_BINDING_2D
                                        : GL_TEXTURE_BINDING_EXTERNAL_OES,
                &saved_texture);
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(target, texture);
  egl->gpu()->ImageTargetTexture2D(target, buffer->image());
  glBindTexture(target, saved_texture);
  bool drawn = draw(target, texture);
  // The texture is kept until the GPU has finished drawing with it.
  glDeleteTextures(1, &texture);
  if (!drawn) {
    return 0;
  }
  AddUse(buffer);
  return buffer->serial();
}

void KmsPlatformView::EndUse(uint64_t serial) {
  std::function<void()> release;
  {
    std::scoped_lock lock(mutex_);
    Buffer* buffer = FindBuffer(serial);
    if (buffer != nullptr) {
      release = RemoveUse(buffer);
    }
  }
  if (release) {
    release();
  }
}

}  // namespace flutter
