// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_next/wayland/wayland_window.h"

#include <algorithm>
#include <cstring>

#include "flutter/fml/logging.h"

namespace flutter {

namespace {

// Maximum number of buffers to allocate. Three allows one to be shown, one
// to be queued in the compositor and one to be drawn into.
constexpr size_t kMaxBuffers = 3;

constexpr int kBytesPerPixel = 4;

}  // namespace

const xdg_surface_listener WaylandWindow::kXdgSurfaceListener = {
    .configure = XdgSurfaceConfigure,
};

const xdg_toplevel_listener WaylandWindow::kXdgToplevelListener = {
    .configure = XdgToplevelConfigure,
    .close = XdgToplevelClose,
};

WaylandWindow::WaylandWindow(WaylandDisplay* display,
                             Delegate* delegate,
                             const Params& params)
    : display_(display),
      delegate_(delegate),
      width_(params.width),
      height_(params.height) {
  surface_ = wl_compositor_create_surface(display_->compositor());
  xdg_surface_ = xdg_wm_base_get_xdg_surface(display_->wm_base(), surface_);
  xdg_surface_add_listener(xdg_surface_, &kXdgSurfaceListener, this);
  xdg_toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
  xdg_toplevel_add_listener(xdg_toplevel_, &kXdgToplevelListener, this);
  if (!params.application_id.empty()) {
    xdg_toplevel_set_app_id(xdg_toplevel_, params.application_id.c_str());
  }

  // Commit without a buffer to get the compositor to send the initial
  // configure event.
  wl_surface_commit(surface_);
}

WaylandWindow::~WaylandWindow() {
  // Buffers must be destroyed while the display is connected.
  buffers_.clear();
  xdg_toplevel_destroy(xdg_toplevel_);
  xdg_surface_destroy(xdg_surface_);
  wl_surface_destroy(surface_);
}

void WaylandWindow::SetTitle(const std::string& title) {
  xdg_toplevel_set_title(xdg_toplevel_, title.c_str());
}

void WaylandWindow::Present(const uint8_t* pixels,
                            int width,
                            int height,
                            size_t row_bytes) {
  if (width <= 0 || height <= 0) {
    return;
  }

  // Keep the frame until it can be shown. Only the newest frame matters, so
  // this replaces any frame that is already waiting.
  size_t stride = static_cast<size_t>(width) * kBytesPerPixel;
  pending_frame_.resize(stride * height);
  for (int y = 0; y < height; y++) {
    memcpy(pending_frame_.data() + y * stride, pixels + y * row_bytes, stride);
  }
  pending_frame_width_ = width;
  pending_frame_height_ = height;

  PresentPendingFrame();
}

ShmBuffer* WaylandWindow::GetFreeBuffer(int width, int height) {
  // Drop buffers that are the wrong size once the compositor is done with
  // them.
  buffers_.erase(std::remove_if(buffers_.begin(), buffers_.end(),
                                [width, height](const auto& buffer) {
                                  return !buffer->busy() &&
                                         (buffer->width() != width ||
                                          buffer->height() != height);
                                }),
                 buffers_.end());

  for (const auto& buffer : buffers_) {
    if (!buffer->busy()) {
      return buffer.get();
    }
  }

  if (buffers_.size() >= kMaxBuffers) {
    return nullptr;
  }

  auto buffer = ShmBuffer::Create(display_->shm(), width, height,
                                  [this]() { PresentPendingFrame(); });
  if (buffer == nullptr) {
    return nullptr;
  }
  buffers_.push_back(std::move(buffer));
  return buffers_.back().get();
}

void WaylandWindow::PresentPendingFrame() {
  if (!configured_ || pending_frame_.empty()) {
    return;
  }

  ShmBuffer* buffer =
      GetFreeBuffer(pending_frame_width_, pending_frame_height_);
  if (buffer == nullptr) {
    return;
  }

  memcpy(buffer->data(), pending_frame_.data(), pending_frame_.size());
  pending_frame_.clear();

  buffer->set_busy();
  wl_surface_attach(surface_, buffer->buffer(), 0, 0);
  wl_surface_damage_buffer(surface_, 0, 0, buffer->width(), buffer->height());
  wl_surface_commit(surface_);
}

void WaylandWindow::XdgSurfaceConfigure(void* data,
                                        xdg_surface* xdg_surface,
                                        uint32_t serial) {
  auto* self = static_cast<WaylandWindow*>(data);
  xdg_surface_ack_configure(xdg_surface, serial);

  // A size of zero means the client chooses the size.
  bool resized = false;
  if (self->pending_width_ > 0 && self->pending_height_ > 0 &&
      (self->pending_width_ != self->width_ ||
       self->pending_height_ != self->height_)) {
    self->width_ = self->pending_width_;
    self->height_ = self->pending_height_;
    resized = true;
  }

  bool first_configure = !self->configured_;
  self->configured_ = true;

  if (resized || first_configure) {
    self->delegate_->OnWindowConfigure();
  }

  self->PresentPendingFrame();
}

void WaylandWindow::XdgToplevelConfigure(void* data,
                                         xdg_toplevel* xdg_toplevel,
                                         int32_t width,
                                         int32_t height,
                                         wl_array* states) {
  auto* self = static_cast<WaylandWindow*>(data);
  self->pending_width_ = width;
  self->pending_height_ = height;
}

void WaylandWindow::XdgToplevelClose(void* data, xdg_toplevel* xdg_toplevel) {
  static_cast<WaylandWindow*>(data)->delegate_->OnWindowClose();
}

}  // namespace flutter
