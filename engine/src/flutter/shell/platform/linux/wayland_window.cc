// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland_window.h"

#include <cmath>

#include "flutter/fml/logging.h"

namespace flutter {

namespace {

// The fractional scale protocol expresses scales in 120ths.
constexpr double kFractionalScaleDenominator = 120.0;

}  // namespace

const wl_surface_listener WaylandWindow::kSurfaceListener = {
    .enter = SurfaceEnter,
    .leave = SurfaceLeave,
};

const xdg_surface_listener WaylandWindow::kXdgSurfaceListener = {
    .configure = XdgSurfaceConfigure,
};

const xdg_toplevel_listener WaylandWindow::kToplevelListener = {
    .configure = ToplevelConfigure,
    .close = ToplevelClose,
    .configure_bounds = ToplevelConfigureBounds,
    .wm_capabilities = ToplevelWmCapabilities,
};

const wp_presentation_feedback_listener WaylandWindow::kFeedbackListener = {
    .sync_output = FeedbackSyncOutput,
    .presented = FeedbackPresented,
    .discarded = FeedbackDiscarded,
};

const wp_fractional_scale_v1_listener WaylandWindow::kFractionalScaleListener =
    {
        .preferred_scale = PreferredScale,
};

std::unique_ptr<WaylandWindow> WaylandWindow::Create(WaylandDisplay* display,
                                                     Delegate* delegate,
                                                     int width,
                                                     int height) {
  std::unique_ptr<WaylandWindow> window(new WaylandWindow(display, delegate));
  if (!window->Initialize(width, height)) {
    return nullptr;
  }
  return window;
}

WaylandWindow::WaylandWindow(WaylandDisplay* display, Delegate* delegate)
    : display_(display), delegate_(delegate) {}

WaylandWindow::~WaylandWindow() {
  {
    std::lock_guard<std::mutex> lock(feedback_mutex_);
    for (struct wp_presentation_feedback* feedback : pending_feedback_) {
      wp_presentation_feedback_destroy(feedback);
    }
    pending_feedback_.clear();
  }
  if (syncobj_surface_ != nullptr) {
    wp_linux_drm_syncobj_surface_v1_destroy(syncobj_surface_);
  }
  if (fractional_scale_ != nullptr) {
    wp_fractional_scale_v1_destroy(fractional_scale_);
  }
  if (viewport_ != nullptr) {
    wp_viewport_destroy(viewport_);
  }
  if (toplevel_ != nullptr) {
    xdg_toplevel_destroy(toplevel_);
  }
  if (xdg_surface_ != nullptr) {
    xdg_surface_destroy(xdg_surface_);
  }
  if (surface_ != nullptr) {
    wl_surface_destroy(surface_);
  }
  wl_display_flush(display_->display());
}

bool WaylandWindow::Initialize(int width, int height) {
  width_ = width;
  height_ = height;

  surface_ = wl_compositor_create_surface(display_->compositor());
  wl_surface_add_listener(surface_, &kSurfaceListener, this);

  // OpenGL renders the bottom row first, so have the compositor flip the
  // buffers vertically rather than doing an extra copy.
  wl_surface_set_buffer_transform(surface_, WL_OUTPUT_TRANSFORM_FLIPPED_180);

  // Fractional scaling works by rendering at the scaled size and having the
  // viewport scale back to the logical size.
  if (display_->viewporter() != nullptr) {
    viewport_ = wp_viewporter_get_viewport(display_->viewporter(), surface_);
    if (display_->fractional_scale_manager() != nullptr) {
      fractional_scale_ = wp_fractional_scale_manager_v1_get_fractional_scale(
          display_->fractional_scale_manager(), surface_);
      wp_fractional_scale_v1_add_listener(fractional_scale_,
                                          &kFractionalScaleListener, this);
    }
  }

  xdg_surface_ = xdg_wm_base_get_xdg_surface(display_->wm_base(), surface_);
  xdg_surface_add_listener(xdg_surface_, &kXdgSurfaceListener, this);
  toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
  xdg_toplevel_add_listener(toplevel_, &kToplevelListener, this);

  // Commit without a buffer to have the compositor configure the window.
  wl_surface_commit(surface_);
  while (!configured_) {
    if (wl_display_roundtrip(display_->display()) < 0) {
      FML_LOG(ERROR) << "Failed to configure window";
      return false;
    }
  }

  return true;
}

int WaylandWindow::physical_width() const {
  return static_cast<int>(std::round(width_ * scale_));
}

int WaylandWindow::physical_height() const {
  return static_cast<int>(std::round(height_ * scale_));
}

void WaylandWindow::SetTitle(const std::string& title) {
  xdg_toplevel_set_title(toplevel_, title.c_str());
}

void WaylandWindow::SetApplicationId(const std::string& application_id) {
  xdg_toplevel_set_app_id(toplevel_, application_id.c_str());
}

void WaylandWindow::Present(wl_buffer* buffer,
                            int width,
                            int height,
                            const SyncPoints* sync) {
  if (sync != nullptr) {
    if (syncobj_surface_ == nullptr) {
      syncobj_surface_ = wp_linux_drm_syncobj_manager_v1_get_surface(
          display_->syncobj_manager(), surface_);
    }
    wp_linux_drm_syncobj_surface_v1_set_acquire_point(
        syncobj_surface_, sync->timeline, sync->acquire_point >> 32,
        sync->acquire_point & 0xffffffff);
    wp_linux_drm_syncobj_surface_v1_set_release_point(
        syncobj_surface_, sync->timeline, sync->release_point >> 32,
        sync->release_point & 0xffffffff);
  }
  wl_surface_attach(surface_, buffer, 0, 0);
  wl_surface_damage_buffer(surface_, 0, 0, width, height);
  if (viewport_ != nullptr) {
    double scale = scale_;
    wp_viewport_set_destination(viewport_,
                                static_cast<int>(std::round(width / scale)),
                                static_cast<int>(std::round(height / scale)));
  }
  if (display_->presentation() != nullptr) {
    // The feedback is created on the default queue, so its events are
    // delivered on the platform thread. No events are sent before the commit,
    // so the listener can be safely added here.
    std::lock_guard<std::mutex> lock(feedback_mutex_);
    struct wp_presentation_feedback* feedback =
        wp_presentation_feedback(display_->presentation(), surface_);
    wp_presentation_feedback_add_listener(feedback, &kFeedbackListener, this);
    pending_feedback_.insert(feedback);
  }
  wl_surface_commit(surface_);
  wl_display_flush(display_->display());
}

void WaylandWindow::RemoveFeedback(struct wp_presentation_feedback* feedback) {
  std::lock_guard<std::mutex> lock(feedback_mutex_);
  pending_feedback_.erase(feedback);
  wp_presentation_feedback_destroy(feedback);
}

void WaylandWindow::FeedbackSyncOutput(
    void* data,
    struct wp_presentation_feedback* feedback,
    wl_output* output) {}

void WaylandWindow::FeedbackPresented(void* data,
                                      struct wp_presentation_feedback* feedback,
                                      uint32_t tv_sec_hi,
                                      uint32_t tv_sec_lo,
                                      uint32_t tv_nsec,
                                      uint32_t refresh,
                                      uint32_t seq_hi,
                                      uint32_t seq_lo,
                                      uint32_t flags) {
  auto* self = static_cast<WaylandWindow*>(data);
  self->RemoveFeedback(feedback);
  uint64_t seconds = (static_cast<uint64_t>(tv_sec_hi) << 32) | tv_sec_lo;
  self->delegate_->OnWindowPresented(
      self->display_->PresentationTimeToMonotonic(seconds, tv_nsec), refresh);
}

void WaylandWindow::FeedbackDiscarded(
    void* data,
    struct wp_presentation_feedback* feedback) {
  static_cast<WaylandWindow*>(data)->RemoveFeedback(feedback);
}

void WaylandWindow::SurfaceEnter(void* data,
                                 wl_surface* surface,
                                 wl_output* output) {}

void WaylandWindow::SurfaceLeave(void* data,
                                 wl_surface* surface,
                                 wl_output* output) {}

void WaylandWindow::XdgSurfaceConfigure(void* data,
                                        xdg_surface* xdg_surface,
                                        uint32_t serial) {
  auto* self = static_cast<WaylandWindow*>(data);
  xdg_surface_ack_configure(xdg_surface, serial);

  // A size of zero means the client chooses, so keep the current size.
  bool changed = false;
  if (self->pending_width_ > 0 && self->pending_width_ != self->width_) {
    self->width_ = self->pending_width_;
    changed = true;
  }
  if (self->pending_height_ > 0 && self->pending_height_ != self->height_) {
    self->height_ = self->pending_height_;
    changed = true;
  }

  bool first_configure = !self->configured_;
  self->configured_ = true;
  if (changed || first_configure) {
    self->delegate_->OnWindowConfigure();
  }
}

void WaylandWindow::ToplevelConfigure(void* data,
                                      xdg_toplevel* toplevel,
                                      int32_t width,
                                      int32_t height,
                                      wl_array* states) {
  auto* self = static_cast<WaylandWindow*>(data);
  self->pending_width_ = width;
  self->pending_height_ = height;
}

void WaylandWindow::ToplevelClose(void* data, xdg_toplevel* toplevel) {
  static_cast<WaylandWindow*>(data)->delegate_->OnWindowClose();
}

void WaylandWindow::ToplevelConfigureBounds(void* data,
                                            xdg_toplevel* toplevel,
                                            int32_t width,
                                            int32_t height) {}

void WaylandWindow::ToplevelWmCapabilities(void* data,
                                           xdg_toplevel* toplevel,
                                           wl_array* capabilities) {}

void WaylandWindow::PreferredScale(void* data,
                                   wp_fractional_scale_v1* fractional_scale,
                                   uint32_t scale) {
  auto* self = static_cast<WaylandWindow*>(data);
  double new_scale = scale / kFractionalScaleDenominator;
  if (new_scale == self->scale_) {
    return;
  }
  self->scale_ = new_scale;
  if (self->configured_) {
    self->delegate_->OnWindowConfigure();
  }
}

}  // namespace flutter
