// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/kms/kms_window.h"

#include <drm_fourcc.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <tuple>

#include "flutter/fml/logging.h"
#include "flutter/fml/unique_fd.h"
#include "flutter/shell/platform/linux/egl_manager.h"
#include "flutter/shell/platform/linux/gpu_device.h"
#include "flutter/shell/platform/linux/kms/kms_display.h"
#include "flutter/shell/platform/linux/kms/kms_seat.h"
#include "flutter/shell/platform/linux/texture_blitter.h"
#include "flutter/shell/platform/linux/texture_buffer.h"

namespace flutter {

namespace {

// Enough to have a buffer on the screen, one waiting for a page flip and one
// being drawn.
constexpr size_t kMaxBuffers = 3;

uint64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

// A buffer the display hardware scans out, drawn to through a framebuffer.
struct KmsWindow::Buffer {
  GpuDevice* gpu = nullptr;
  gbm_bo* bo = nullptr;
  EGLImageKHR image = EGL_NO_IMAGE_KHR;
  GLuint texture = 0;
  GLuint framebuffer = 0;

  // The buffer's handle and framebuffer on the display device.
  uint32_t handle = 0;
  uint32_t fb_id = 0;

  // Must be destroyed with the render context current.
  ~Buffer() {
    if (framebuffer != 0) {
      glDeleteFramebuffers(1, &framebuffer);
    }
    if (texture != 0) {
      glDeleteTextures(1, &texture);
    }
    if (image != EGL_NO_IMAGE_KHR) {
      gpu->DestroyImage(gpu->display(), image);
    }
    if (bo != nullptr) {
      gbm_bo_destroy(bo);
    }
  }

  void ReleaseFromDisplay(int drm_fd) {
    if (fb_id != 0) {
      drmModeRmFB(drm_fd, fb_id);
      fb_id = 0;
    }
    if (handle != 0) {
      drm_gem_close close = {.handle = handle};
      drmIoctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &close);
      handle = 0;
    }
  }
};

KmsWindow::KmsWindow(KmsDisplay* display,
                     Delegate* delegate,
                     const Params& params)
    : display_(display), delegate_(delegate), title_(params.title) {
  active_ = display_->active();
}

KmsWindow::~KmsWindow() {
  display_->RemoveWindow(this);
  display_->seat()->SetHandlers(nullptr, nullptr);

  std::vector<std::unique_ptr<Buffer>> buffers;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    buffers = std::move(buffers_);
    front_ = pending_ = queued_ = nullptr;
  }
  // Removing the framebuffer on the screen turns it off. The display
  // restores what was shown before when it is destroyed.
  for (auto& buffer : buffers) {
    buffer->ReleaseFromDisplay(display_->drm_fd());
  }

  // GPU resources must be freed with the render context current.
  auto resources =
      std::make_shared<std::tuple<std::vector<std::unique_ptr<Buffer>>,
                                  std::unique_ptr<TextureBlitter>>>(
          std::move(buffers), std::move(blitter_));
  delegate_->RunWithRenderContext([resources]() mutable { resources.reset(); });
}

int KmsWindow::width() const {
  return std::max(
      static_cast<int>(std::lround(display_->mode().hdisplay / scale())), 1);
}

int KmsWindow::height() const {
  return std::max(
      static_cast<int>(std::lround(display_->mode().vdisplay / scale())), 1);
}

double KmsWindow::scale() const {
  return display_->scale();
}

uint32_t KmsWindow::display_id() const {
  return display_->connector_id();
}

int KmsWindow::physical_width() const {
  return display_->mode().hdisplay;
}

int KmsWindow::physical_height() const {
  return display_->mode().vdisplay;
}

bool KmsWindow::activated() const {
  return display_->active();
}

bool KmsWindow::suspended() const {
  return !display_->active();
}

bool KmsWindow::has_pointer_focus() const {
  return display_->seat()->pointer_focused();
}

bool KmsWindow::has_keyboard_focus() const {
  return display_->seat()->keyboard_focused();
}

void KmsWindow::SetInputHandlers(InputHandler* input_handler,
                                 KeyboardHandler* keyboard_handler,
                                 InputMethodHandler* input_method_handler) {
  // There is no input method without a display server.
  display_->seat()->SetHandlers(input_handler, keyboard_handler);
}

size_t KmsWindow::frames_in_flight() {
  uint64_t presented = frames_presented_;
  uint64_t completed = frames_completed_;
  return presented > completed ? presented - completed : 0;
}

void KmsWindow::Present(RenderBuffer* buffer,
                        const SyncPoints* sync,
                        const std::vector<Layer>& layers) {
  // Platform views aren't supported, so there are no layers. The compositor
  // renders into textures for this backend.
  auto* source = static_cast<TextureBuffer*>(buffer);
  Buffer* target = AcquireBuffer(source);
  if (target == nullptr) {
    return;
  }

  if (blitter_ == nullptr) {
    blitter_ = std::make_unique<TextureBlitter>();
  }
  // The frame is scaled to the screen if it was drawn at a different size.
  if (!blitter_->Draw(source->texture(), target->framebuffer, physical_width(),
                      physical_height(),
                      /*swap_red_blue=*/false)) {
    return;
  }
  // The display reads the buffer without synchronizing with this context.
  glFinish();

  std::lock_guard<std::mutex> lock(mutex_);
  if (queued_ != nullptr) {
    // Replace a frame that hasn't been shown yet.
    frames_completed_++;
  }
  queued_ = target;
  frames_presented_++;
  if (active_ && pending_ == nullptr) {
    Submit();
  }
}

KmsWindow::Buffer* KmsWindow::AcquireBuffer(TextureBuffer* source) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& buffer : buffers_) {
      Buffer* candidate = buffer.get();
      if (candidate != front_ && candidate != pending_ &&
          candidate != queued_) {
        return candidate;
      }
    }
    if (buffers_.size() >= kMaxBuffers) {
      // All buffers are in use, so draw over the frame that is waiting to be
      // shown. It is replaced when this frame is queued.
      Buffer* target = queued_;
      if (target != nullptr) {
        queued_ = nullptr;
        frames_completed_++;
      }
      return target;
    }
  }

  // Buffers are only created and destroyed on this thread, so the lock
  // isn't needed while creating one.
  std::unique_ptr<Buffer> created = CreateBuffer(source);
  if (created == nullptr) {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  Buffer* target = created.get();
  buffers_.push_back(std::move(created));
  return target;
}

std::unique_ptr<KmsWindow::Buffer> KmsWindow::CreateBuffer(
    TextureBuffer* source) {
  GpuDevice* gpu = source->egl_manager()->gpu();
  if (!gpu->supports_dmabuf_import()) {
    if (!reported_error_) {
      FML_LOG(ERROR) << "The GPU can't import buffers for the display";
      reported_error_ = true;
    }
    return nullptr;
  }
  int width = physical_width();
  int height = physical_height();
  int drm_fd = display_->drm_fd();

  // The buffer is allocated by the GPU that renders and imported into the
  // display device, which is usually the same GPU.
  auto buffer = std::make_unique<Buffer>();
  buffer->gpu = gpu;
  buffer->bo = gbm_bo_create(gpu->gbm(), width, height, GBM_FORMAT_XRGB8888,
                             GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
  if (buffer->bo == nullptr || gbm_bo_get_plane_count(buffer->bo) != 1) {
    FML_LOG(ERROR) << "Failed to allocate a buffer for the display";
    return nullptr;
  }
  fml::UniqueFD fd(gbm_bo_get_fd(buffer->bo));
  if (!fd.is_valid()) {
    return nullptr;
  }
  uint32_t stride = gbm_bo_get_stride(buffer->bo);
  uint64_t modifier =
      gpu->supports_dmabuf_modifiers() && display_->supports_modifiers()
          ? gbm_bo_get_modifier(buffer->bo)
          : DRM_FORMAT_MOD_INVALID;

  GpuDevice::DmabufPlane plane = {fd.get(), 0, stride};
  buffer->image = gpu->ImportDmabuf(width, height, DRM_FORMAT_XRGB8888,
                                    modifier, &plane, 1);
  if (buffer->image == EGL_NO_IMAGE_KHR) {
    FML_LOG(ERROR) << "Failed to import a buffer for the display";
    return nullptr;
  }
  GLint saved_texture = 0;
  GLint saved_framebuffer = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_texture);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_framebuffer);
  glGenTextures(1, &buffer->texture);
  glBindTexture(GL_TEXTURE_2D, buffer->texture);
  gpu->ImageTargetTexture2D(GL_TEXTURE_2D, buffer->image);
  glGenFramebuffers(1, &buffer->framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, buffer->framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         buffer->texture, 0);
  bool complete =
      glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindTexture(GL_TEXTURE_2D, saved_texture);
  glBindFramebuffer(GL_FRAMEBUFFER, saved_framebuffer);
  if (!complete) {
    return nullptr;
  }

  if (drmPrimeFDToHandle(drm_fd, fd.get(), &buffer->handle) != 0) {
    FML_LOG(ERROR) << "Failed to import a buffer into the display device: "
                   << strerror(errno);
    return nullptr;
  }
  uint32_t handles[4] = {buffer->handle};
  uint32_t pitches[4] = {stride};
  uint32_t offsets[4] = {0};
  int result;
  if (modifier != DRM_FORMAT_MOD_INVALID) {
    uint64_t modifiers[4] = {modifier};
    result = drmModeAddFB2WithModifiers(
        drm_fd, width, height, DRM_FORMAT_XRGB8888, handles, pitches, offsets,
        modifiers, &buffer->fb_id, DRM_MODE_FB_MODIFIERS);
  } else {
    result = drmModeAddFB2(drm_fd, width, height, DRM_FORMAT_XRGB8888, handles,
                           pitches, offsets, &buffer->fb_id, 0);
  }
  if (result != 0) {
    FML_LOG(ERROR) << "Failed to create a framebuffer for the display: "
                   << strerror(errno);
    buffer->ReleaseFromDisplay(drm_fd);
    return nullptr;
  }
  return buffer;
}

void KmsWindow::Submit() {
  Buffer* buffer = queued_;
  queued_ = nullptr;
  int drm_fd = display_->drm_fd();

  if (needs_modeset_) {
    uint32_t connector = display_->connector_id();
    drmModeModeInfo mode = display_->mode();
    if (drmModeSetCrtc(drm_fd, display_->crtc_id(), buffer->fb_id, 0, 0,
                       &connector, 1, &mode) != 0) {
      if (!reported_error_) {
        FML_LOG(ERROR) << "Failed to set the display mode: " << strerror(errno);
        reported_error_ = true;
      }
      frames_completed_++;
      PostFrameDone(false, 0);
      return;
    }
    needs_modeset_ = false;
    front_ = buffer;
    frames_completed_++;
    PostFrameDone(true, NowNs());
    return;
  }

  if (drmModePageFlip(drm_fd, display_->crtc_id(), buffer->fb_id,
                      DRM_MODE_PAGE_FLIP_EVENT, display_) != 0) {
    if (!reported_error_) {
      FML_LOG(ERROR) << "Failed to show a frame: " << strerror(errno);
      reported_error_ = true;
    }
    frames_completed_++;
    PostFrameDone(false, 0);
    return;
  }
  pending_ = buffer;
}

void KmsWindow::PostFrameDone(bool presented, uint64_t time_ns) {
  display_->event_loop()->PostTask(
      [this, alive = std::weak_ptr<bool>(alive_), presented, time_ns]() {
        if (alive.expired()) {
          return;
        }
        if (presented) {
          // The screen was configured, which hides the cursor.
          display_->RefreshCursor();
          delegate_->OnWindowPresented(time_ns, display_->refresh_ns());
        }
        delegate_->OnWindowFrameDone();
      });
}

void KmsWindow::OnPageFlip(uint64_t time_ns) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_ == nullptr) {
      return;
    }
    front_ = pending_;
    pending_ = nullptr;
    frames_completed_++;
    if (queued_ != nullptr && active_) {
      Submit();
    }
  }
  delegate_->OnWindowPresented(time_ns, display_->refresh_ns());
  delegate_->OnWindowFrameDone();
}

void KmsWindow::Suspend() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = false;
    needs_modeset_ = true;
    // The page flip event may not arrive once the display is released.
    if (pending_ != nullptr) {
      front_ = pending_;
      pending_ = nullptr;
      frames_completed_++;
    }
  }
  delegate_->OnWindowStateChanged();
}

void KmsWindow::Resume() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = true;
    needs_modeset_ = true;
    reported_error_ = false;
    if (queued_ != nullptr) {
      Submit();
    } else if (front_ != nullptr) {
      // Show the last frame again until a new one is drawn.
      uint32_t connector = display_->connector_id();
      drmModeModeInfo mode = display_->mode();
      if (drmModeSetCrtc(display_->drm_fd(), display_->crtc_id(), front_->fb_id,
                         0, 0, &connector, 1, &mode) == 0) {
        needs_modeset_ = false;
      }
    }
  }
  delegate_->OnWindowStateChanged();
}

}  // namespace flutter
