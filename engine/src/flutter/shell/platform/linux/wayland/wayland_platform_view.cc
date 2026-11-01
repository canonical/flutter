// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland/wayland_platform_view.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/wayland/wayland_window.h"

namespace flutter {

// A buffer shared with the compositor.
class WaylandPlatformView::WaylandBuffer : public LinuxPlatformView::Buffer {
 public:
  WaylandBuffer(std::shared_ptr<WaylandPlatformView> view,
                const DmabufFrame& frame,
                wl_buffer* buffer)
      : Buffer(view, frame), view_(view.get()), buffer_(buffer) {
    wl_buffer_add_listener(buffer_, &kListener, this);
  }

  ~WaylandBuffer() override {
    Remove();
    wl_buffer_destroy(buffer_);
  }

  wl_buffer* buffer() const { return buffer_; }

 private:
  static void OnRelease(void* data, wl_buffer* buffer) {
    auto* self = static_cast<WaylandBuffer*>(data);
    self->view_->OnBufferReleased(self);
  }

  static constexpr wl_buffer_listener kListener = {
      .release = OnRelease,
  };

  WaylandPlatformView* view_;
  wl_buffer* buffer_;
};

WaylandPlatformView::WaylandPlatformView(int64_t id,
                                         WaylandDisplay* display,
                                         EventLoop* event_loop)
    : LinuxPlatformView(id, event_loop), display_(display) {}

WaylandPlatformView::~WaylandPlatformView() {
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

std::unique_ptr<LinuxPlatformView::Buffer>
WaylandPlatformView::CreateBackendBuffer(const DmabufFrame& frame) {
  if (display_->dmabuf() == nullptr) {
    FML_LOG(ERROR) << "Platform view buffers require linux_dmabuf";
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
  wl_buffer* buffer = zwp_linux_buffer_params_v1_create_immed(
      params, frame.width, frame.height, frame.format, 0);
  zwp_linux_buffer_params_v1_destroy(params);
  return std::make_unique<WaylandBuffer>(
      std::static_pointer_cast<WaylandPlatformView>(shared_from_this()), frame,
      buffer);
}

void WaylandPlatformView::OnPresent() {
  WaylandWindow* window;
  {
    std::scoped_lock lock(mutex_);
    window = window_;
  }
  // The window can't be removed while this runs, as that is also done on
  // the platform thread.
  if (window != nullptr) {
    window->CommitPlatformView(this);
  }
}

void WaylandPlatformView::OnDispose() {
  WaylandWindow* window;
  {
    std::scoped_lock lock(mutex_);
    window = window_;
  }
  if (window != nullptr) {
    window->RemovePlatformView(this);
  }

  std::vector<std::function<void()>> released;
  {
    std::scoped_lock lock(mutex_);
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
    released = RemoveAllUses();
  }
  for (const auto& release : released) {
    release();
  }
}

void WaylandPlatformView::OnBufferRemoved(Buffer* buffer) {
  // The surface keeps showing the buffer's contents.
  if (attached_ == buffer) {
    attached_ = nullptr;
  }
}

wl_surface* WaylandPlatformView::Show(WaylandWindow* window,
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
    FML_LOG(WARNING) << "Platform view " << id()
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
  ReportSize(width, height, scale);
  return surface_;
}

void WaylandPlatformView::Hide(WaylandWindow* window) {
  std::scoped_lock lock(mutex_);
  if (window_ != window || !shown_) {
    return;
  }
  shown_ = false;
  Apply();
}

void WaylandPlatformView::Detach(WaylandWindow* window) {
  std::scoped_lock lock(mutex_);
  if (window_ == window) {
    DestroySubsurface();
  }
}

bool WaylandPlatformView::IsInWindow(WaylandWindow* window) {
  std::scoped_lock lock(mutex_);
  return window_ == window;
}

bool WaylandPlatformView::CommitBuffer(WaylandWindow* window) {
  std::scoped_lock lock(mutex_);
  if (window_ != window || !shown_) {
    return false;
  }
  Apply();
  return true;
}

void WaylandPlatformView::DestroySubsurface() {
  if (subsurface_ != nullptr) {
    wl_subsurface_destroy(subsurface_);
    subsurface_ = nullptr;
  }
  window_ = nullptr;
  shown_ = false;
  placed_ = false;
}

void WaylandPlatformView::Apply() {
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
    wl_surface_attach(surface_, static_cast<WaylandBuffer*>(current_)->buffer(),
                      0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, current_->width(),
                             current_->height());
    // The compositor releases a buffer once, however often it is attached.
    if (!InUse(current_)) {
      AddUse(current_);
    }
    attached_ = current_;
    mapped_ = true;
  }
  wl_surface_commit(surface_);
}

void WaylandPlatformView::OnBufferReleased(WaylandBuffer* buffer) {
  std::function<void()> release;
  {
    std::scoped_lock lock(mutex_);
    release = RemoveUse(buffer);
  }
  if (release) {
    release();
  }
}

}  // namespace flutter
