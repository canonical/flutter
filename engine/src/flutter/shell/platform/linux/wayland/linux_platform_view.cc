// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland/linux_platform_view.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/wayland/wayland_window.h"

namespace flutter {

const wl_buffer_listener LinuxPlatformView::Buffer::kListener = {
    .release = LinuxPlatformView::Buffer::OnRelease,
};

LinuxPlatformView::Buffer::Buffer(std::shared_ptr<LinuxPlatformView> view,
                                  wl_buffer* buffer,
                                  int width,
                                  int height,
                                  std::function<void()> release)
    : view_(std::move(view)),
      buffer_(buffer),
      width_(width),
      height_(height),
      release_(std::move(release)) {}

LinuxPlatformView::Buffer::~Buffer() {
  view_->RemoveBuffer(this);
  wl_buffer_destroy(buffer_);
}

void LinuxPlatformView::Buffer::OnRelease(void* data, wl_buffer* buffer) {
  auto* self = static_cast<Buffer*>(data);
  self->view_->OnBufferReleased(self);
}

LinuxPlatformView::LinuxPlatformView(int64_t id,
                                     WaylandDisplay* display,
                                     EventLoop* event_loop)
    : id_(id), display_(display), event_loop_(event_loop) {}

LinuxPlatformView::~LinuxPlatformView() {
  // Buffers hold a reference to the view, so they have all been freed.
  FML_DCHECK(buffers_.empty());
  FML_DCHECK(window_ == nullptr);
  if (alpha_ != nullptr) {
    wp_alpha_modifier_surface_v1_destroy(alpha_);
  }
  if (viewport_ != nullptr) {
    wp_viewport_destroy(viewport_);
  }
  if (surface_ != nullptr) {
    wl_surface_destroy(surface_);
  }
}

void LinuxPlatformView::SetCallbacks(ResizeCallback on_resize,
                                     PointerCallback on_pointer) {
  on_resize_ = std::move(on_resize);
  on_pointer_ = std::move(on_pointer);
  if (on_resize_ && shown_width_ > 0) {
    on_resize_(shown_width_, shown_height_, shown_scale_);
  }
}

std::unique_ptr<LinuxPlatformView::Buffer> LinuxPlatformView::CreateBuffer(
    const DmabufFrame& frame) {
  if (display_->dmabuf() == nullptr) {
    FML_LOG(ERROR) << "Platform view buffers require linux_dmabuf";
    return nullptr;
  }
  if (frame.width == 0 || frame.height == 0 || frame.planes.empty() ||
      frame.planes.size() > 4 ||
      frame.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
      frame.height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    FML_LOG(ERROR) << "Invalid platform view buffer";
    return nullptr;
  }
  // The compositor disconnects clients that use unsupported buffers.
  std::vector<uint64_t> modifiers = display_->GetModifiers(frame.format);
  if (std::find(modifiers.begin(), modifiers.end(), frame.modifier) ==
      modifiers.end()) {
    FML_LOG(ERROR) << "The Wayland compositor does not support format 0x"
                   << std::hex << frame.format << " with modifier 0x"
                   << frame.modifier;
    return nullptr;
  }

  zwp_linux_buffer_params_v1* params =
      zwp_linux_dmabuf_v1_create_params(display_->dmabuf());
  for (size_t i = 0; i < frame.planes.size(); i++) {
    const DmabufFrame::Plane& plane = frame.planes[i];
    zwp_linux_buffer_params_v1_add(params, plane.fd, i, plane.offset,
                                   plane.stride, frame.modifier >> 32,
                                   frame.modifier & 0xffffffff);
  }
  wl_buffer* wl_buffer = zwp_linux_buffer_params_v1_create_immed(
      params, frame.width, frame.height, frame.format, 0);
  zwp_linux_buffer_params_v1_destroy(params);

  std::unique_ptr<Buffer> buffer(new Buffer(
      shared_from_this(), wl_buffer, frame.width, frame.height, frame.release));
  wl_buffer_add_listener(wl_buffer, &Buffer::kListener, buffer.get());
  std::scoped_lock lock(mutex_);
  buffers_.push_back(buffer.get());
  return buffer;
}

void LinuxPlatformView::Present(Buffer* buffer) {
  if (buffer != nullptr && buffer->view_.get() != this) {
    FML_LOG(ERROR) << "Buffer presented in a different platform view";
    return;
  }
  WaylandWindow* window = nullptr;
  Buffer* replaced = nullptr;
  {
    std::scoped_lock lock(mutex_);
    if (disposed_ || buffer == current_) {
      return;
    }
    replaced = current_;
    current_ = buffer;
    if (buffer != nullptr) {
      buffer->release_pending_ = true;
    }
    if (replaced != nullptr && !TakeRelease(replaced)) {
      replaced = nullptr;
    }
    window = window_;
  }

  // The window can't be removed while this runs, as that is also done on
  // the platform thread.
  if (window != nullptr) {
    window->CommitPlatformView(this);
  }
  if (replaced != nullptr && replaced->release_) {
    replaced->release_();
  }
}

void LinuxPlatformView::DispatchPointerEvent(
    const PlatformViewPointerEvent& event) {
  if (on_pointer_) {
    on_pointer_(event);
  }
}

void LinuxPlatformView::Dispose() {
  WaylandWindow* window;
  {
    std::scoped_lock lock(mutex_);
    disposed_ = true;
    window = window_;
  }
  if (window != nullptr) {
    window->RemovePlatformView(this);
  }

  std::vector<std::function<void()>> released;
  {
    std::scoped_lock lock(mutex_);
    current_ = nullptr;
    attached_ = nullptr;
    mapped_ = false;
    if (alpha_ != nullptr) {
      wp_alpha_modifier_surface_v1_destroy(alpha_);
      alpha_ = nullptr;
    }
    if (viewport_ != nullptr) {
      wp_viewport_destroy(viewport_);
      viewport_ = nullptr;
    }
    if (surface_ != nullptr) {
      wl_surface_destroy(surface_);
      surface_ = nullptr;
    }
    // Without a surface the compositor no longer uses the buffers.
    for (Buffer* buffer : buffers_) {
      buffer->busy_ = false;
      if (TakeRelease(buffer) && buffer->release_) {
        released.push_back(buffer->release_);
      }
    }
  }
  // Release the buffers before clearing the callbacks, as that may destroy
  // the plugin's view and the buffers with it.
  for (const auto& release : released) {
    release();
  }
  on_resize_ = nullptr;
  on_pointer_ = nullptr;
}

wl_surface* LinuxPlatformView::Show(WaylandWindow* window,
                                    wl_surface* parent,
                                    wl_surface* above,
                                    const PlatformViewGeometry& geometry,
                                    double scale) {
  std::scoped_lock lock(mutex_);
  if (disposed_) {
    return nullptr;
  }
  if (display_->viewporter() == nullptr) {
    static std::once_flag once;
    std::call_once(once, []() {
      FML_LOG(WARNING) << "Platform views require wp_viewporter";
    });
    return nullptr;
  }

  if (window_ != window) {
    DestroySubsurface();
  }
  if (surface_ == nullptr) {
    surface_ = wl_compositor_create_surface(display_->compositor());
    viewport_ = wp_viewporter_get_viewport(display_->viewporter(), surface_);
    if (display_->alpha_modifier() != nullptr) {
      alpha_ = wp_alpha_modifier_v1_get_surface(display_->alpha_modifier(),
                                                surface_);
    }
    // Input goes to the Flutter view, which forwards it through the
    // framework.
    wl_region* region = wl_compositor_create_region(display_->compositor());
    wl_surface_set_input_region(surface_, region);
    wl_region_destroy(region);
  }
  if (subsurface_ == nullptr) {
    subsurface_ = wl_subcompositor_get_subsurface(display_->subcompositor(),
                                                  surface_, parent);
    // Changes are applied with the Flutter frame.
    wl_subsurface_set_sync(subsurface_);
    window_ = window;
    placed_ = false;
  }
  wl_subsurface_place_above(subsurface_, above);

  if (geometry.approximated && !warned_approximated_) {
    FML_LOG(WARNING) << "Platform view " << id_
                     << " is rotated, skewed or has a rounded clip, which "
                        "can't be shown exactly";
    warned_approximated_ = true;
  }

  geometry_ = geometry;
  scale_ = scale;
  shown_ = true;
  Apply();

  int width = static_cast<int>(std::round(geometry.rect.width));
  int height = static_cast<int>(std::round(geometry.rect.height));
  if (width != reported_width_ || height != reported_height_ ||
      scale != reported_scale_) {
    reported_width_ = width;
    reported_height_ = height;
    reported_scale_ = scale;
    event_loop_->PostTask([weak = weak_from_this(), width, height, scale]() {
      if (auto self = weak.lock()) {
        self->shown_width_ = width;
        self->shown_height_ = height;
        self->shown_scale_ = scale;
        if (self->on_resize_ && width > 0 && height > 0) {
          self->on_resize_(width, height, scale);
        }
      }
    });
  }
  return surface_;
}

void LinuxPlatformView::Hide(WaylandWindow* window) {
  std::scoped_lock lock(mutex_);
  if (window_ != window || !shown_) {
    return;
  }
  shown_ = false;
  Apply();
}

void LinuxPlatformView::Detach(WaylandWindow* window) {
  std::scoped_lock lock(mutex_);
  if (window_ == window) {
    DestroySubsurface();
  }
}

bool LinuxPlatformView::IsInWindow(WaylandWindow* window) {
  std::scoped_lock lock(mutex_);
  return window_ == window;
}

bool LinuxPlatformView::CommitBuffer(WaylandWindow* window) {
  std::scoped_lock lock(mutex_);
  if (window_ != window || !shown_) {
    return false;
  }
  Apply();
  return true;
}

void LinuxPlatformView::DestroySubsurface() {
  if (subsurface_ != nullptr) {
    wl_subsurface_destroy(subsurface_);
    subsurface_ = nullptr;
  }
  window_ = nullptr;
  shown_ = false;
  placed_ = false;
}

void LinuxPlatformView::Apply() {
  SurfacePlacement placement;
  bool visible =
      shown_ && current_ != nullptr &&
      PlaceBuffer(current_->width(), current_->height(), geometry_.rect,
                  geometry_.visible, scale_, &placement);
  if (!visible) {
    if (mapped_) {
      wl_surface_attach(surface_, nullptr, 0, 0);
      wl_surface_commit(surface_);
      attached_ = nullptr;
      mapped_ = false;
    }
    return;
  }

  if (!placed_ || placement.x != placement_.x || placement.y != placement_.y) {
    wl_subsurface_set_position(subsurface_, placement.x, placement.y);
  }
  if (!placed_ || !(placement == placement_)) {
    wp_viewport_set_source(viewport_, placement.source_x, placement.source_y,
                           placement.source_width, placement.source_height);
    wp_viewport_set_destination(viewport_, placement.width, placement.height);
  }
  placement_ = placement;
  placed_ = true;

  double opacity = std::clamp(geometry_.opacity, 0.0, 1.0);
  if (alpha_ != nullptr && opacity != opacity_) {
    wp_alpha_modifier_surface_v1_set_multiplier(
        alpha_, static_cast<uint32_t>(std::round(
                    opacity * std::numeric_limits<uint32_t>::max())));
    opacity_ = opacity;
  }

  if (!mapped_ || attached_ != current_) {
    wl_surface_attach(surface_, current_->buffer_, 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, current_->width(),
                             current_->height());
    current_->busy_ = true;
    attached_ = current_;
    mapped_ = true;
  }
  wl_surface_commit(surface_);
}

bool LinuxPlatformView::TakeRelease(Buffer* buffer) {
  if (!buffer->release_pending_ || buffer->busy_ || buffer == current_) {
    return false;
  }
  buffer->release_pending_ = false;
  return true;
}

void LinuxPlatformView::RemoveBuffer(Buffer* buffer) {
  std::scoped_lock lock(mutex_);
  buffers_.erase(std::remove(buffers_.begin(), buffers_.end(), buffer),
                 buffers_.end());
  if (current_ == buffer) {
    current_ = nullptr;
  }
  // The surface keeps showing the buffer's contents.
  if (attached_ == buffer) {
    attached_ = nullptr;
  }
}

void LinuxPlatformView::OnBufferReleased(Buffer* buffer) {
  bool release;
  {
    std::scoped_lock lock(mutex_);
    buffer->busy_ = false;
    release = TakeRelease(buffer);
  }
  if (release && buffer->release_) {
    buffer->release_();
  }
}

}  // namespace flutter
