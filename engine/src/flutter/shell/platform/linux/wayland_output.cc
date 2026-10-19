// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland_output.h"

#include <utility>

namespace flutter {

const wl_output_listener WaylandOutput::kOutputListener = {
    .geometry = Geometry,
    .mode = Mode,
    .done = Done,
    .scale = Scale,
    .name = Name,
    .description = Description,
};

const zxdg_output_v1_listener WaylandOutput::kXdgOutputListener = {
    .logical_position = XdgLogicalPosition,
    .logical_size = XdgLogicalSize,
    .done = XdgDone,
    .name = XdgName,
    .description = XdgDescription,
};

WaylandOutput::WaylandOutput(uint32_t id,
                             wl_output* output,
                             std::function<void()> changed_callback)
    : id_(id), output_(output), changed_callback_(std::move(changed_callback)) {
  wl_output_add_listener(output_, &kOutputListener, this);
}

WaylandOutput::~WaylandOutput() {
  if (xdg_output_ != nullptr) {
    zxdg_output_v1_destroy(xdg_output_);
  }
  if (wl_output_get_version(output_) >= WL_OUTPUT_RELEASE_SINCE_VERSION) {
    wl_output_release(output_);
  } else {
    wl_output_destroy(output_);
  }
}

void WaylandOutput::SetXdgOutputManager(zxdg_output_manager_v1* manager) {
  if (manager == nullptr || xdg_output_ != nullptr) {
    return;
  }
  xdg_output_ = zxdg_output_manager_v1_get_xdg_output(manager, output_);
  zxdg_output_v1_add_listener(xdg_output_, &kXdgOutputListener, this);
}

double WaylandOutput::pixel_ratio() const {
  // The logical size accounts for fractional scaling, which the integer
  // scale is rounded up from.
  if (logical_width_ > 0 && width_ > 0) {
    return static_cast<double>(width_) / logical_width_;
  }
  return scale_;
}

void WaylandOutput::ApplyPending() {
  // Rotated outputs are shown with the mode's width and height swapped.
  bool rotated = (pending_.transform & WL_OUTPUT_TRANSFORM_90) != 0;
  int32_t width = rotated ? pending_.mode_height : pending_.mode_width;
  int32_t height = rotated ? pending_.mode_width : pending_.mode_height;

  bool changed = !ready_ || width != width_ || height != height_ ||
                 pending_.refresh_mhz != refresh_mhz_ ||
                 pending_.scale != scale_ ||
                 pending_.logical_width != logical_width_ ||
                 pending_.name != name_ || pending_.description != description_;
  ready_ = true;
  width_ = width;
  height_ = height;
  refresh_mhz_ = pending_.refresh_mhz;
  scale_ = pending_.scale;
  logical_width_ = pending_.logical_width;
  name_ = pending_.name;
  description_ = pending_.description;
  if (changed && changed_callback_) {
    changed_callback_();
  }
}

void WaylandOutput::Geometry(void* data,
                             wl_output* output,
                             int32_t x,
                             int32_t y,
                             int32_t physical_width,
                             int32_t physical_height,
                             int32_t subpixel,
                             const char* make,
                             const char* model,
                             int32_t transform) {
  static_cast<WaylandOutput*>(data)->pending_.transform = transform;
}

void WaylandOutput::Mode(void* data,
                         wl_output* output,
                         uint32_t flags,
                         int32_t width,
                         int32_t height,
                         int32_t refresh) {
  if ((flags & WL_OUTPUT_MODE_CURRENT) == 0) {
    return;
  }
  auto* self = static_cast<WaylandOutput*>(data);
  self->pending_.mode_width = width;
  self->pending_.mode_height = height;
  self->pending_.refresh_mhz = refresh;
}

void WaylandOutput::Done(void* data, wl_output* output) {
  static_cast<WaylandOutput*>(data)->ApplyPending();
}

void WaylandOutput::Scale(void* data, wl_output* output, int32_t factor) {
  static_cast<WaylandOutput*>(data)->pending_.scale = factor > 0 ? factor : 1;
}

void WaylandOutput::Name(void* data, wl_output* output, const char* name) {
  static_cast<WaylandOutput*>(data)->pending_.name = name;
}

void WaylandOutput::Description(void* data,
                                wl_output* output,
                                const char* description) {
  static_cast<WaylandOutput*>(data)->pending_.description = description;
}

void WaylandOutput::XdgLogicalPosition(void* data,
                                       zxdg_output_v1* xdg_output,
                                       int32_t x,
                                       int32_t y) {}

void WaylandOutput::XdgLogicalSize(void* data,
                                   zxdg_output_v1* xdg_output,
                                   int32_t width,
                                   int32_t height) {
  auto* self = static_cast<WaylandOutput*>(data);
  self->pending_.logical_width = width;
  self->pending_.logical_height = height;
}

void WaylandOutput::XdgDone(void* data, zxdg_output_v1* xdg_output) {
  // Before version 3 changes to the logical size are completed by this
  // rather than wl_output.done.
  auto* self = static_cast<WaylandOutput*>(data);
  if (self->ready_) {
    self->ApplyPending();
  }
}

void WaylandOutput::XdgName(void* data,
                            zxdg_output_v1* xdg_output,
                            const char* name) {
  // Only used if wl_output is too old to provide a name.
  auto* self = static_cast<WaylandOutput*>(data);
  if (wl_output_get_version(self->output_) < WL_OUTPUT_NAME_SINCE_VERSION) {
    self->pending_.name = name;
  }
}

void WaylandOutput::XdgDescription(void* data,
                                   zxdg_output_v1* xdg_output,
                                   const char* description) {
  auto* self = static_cast<WaylandOutput*>(data);
  if (wl_output_get_version(self->output_) <
      WL_OUTPUT_DESCRIPTION_SINCE_VERSION) {
    self->pending_.description = description;
  }
}

}  // namespace flutter
