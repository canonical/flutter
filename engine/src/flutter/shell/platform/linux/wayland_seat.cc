// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland_seat.h"

namespace flutter {

const wl_seat_listener WaylandSeat::kSeatListener = {
    .capabilities = SeatCapabilities,
    .name = SeatName,
};

const wl_pointer_listener WaylandSeat::kPointerListener = {
    .enter = PointerEnter,
    .leave = PointerLeave,
    .motion = PointerMotion,
    .button = PointerButton,
    .axis = PointerAxis,
    .frame = PointerFrame,
    .axis_source = PointerAxisSource,
    .axis_stop = PointerAxisStop,
    .axis_discrete = PointerAxisDiscrete,
};

WaylandSeat::WaylandSeat(wl_seat* seat,
                         wp_cursor_shape_manager_v1* cursor_shape_manager)
    : seat_(seat), cursor_shape_manager_(cursor_shape_manager) {
  wl_seat_add_listener(seat_, &kSeatListener, this);
}

WaylandSeat::~WaylandSeat() {
  if (cursor_shape_device_ != nullptr) {
    wp_cursor_shape_device_v1_destroy(cursor_shape_device_);
  }
  if (pointer_ != nullptr) {
    wl_pointer_destroy(pointer_);
  }
  wl_seat_destroy(seat_);
}

void WaylandSeat::SetPointerHandler(wl_surface* surface,
                                    PointerHandler* handler) {
  if (handler != nullptr) {
    pointer_handlers_[surface] = handler;
    return;
  }
  pointer_handlers_.erase(surface);
  if (pointer_focus_ == surface) {
    pointer_focus_ = nullptr;
  }
}

void WaylandSeat::SetCursor(uint32_t shape) {
  if (pointer_ == nullptr || cursor_shape_manager_ == nullptr ||
      pointer_focus_ == nullptr) {
    return;
  }
  if (cursor_shape_device_ == nullptr) {
    cursor_shape_device_ =
        wp_cursor_shape_manager_v1_get_pointer(cursor_shape_manager_, pointer_);
  }
  wp_cursor_shape_device_v1_set_shape(cursor_shape_device_, enter_serial_,
                                      shape);
}

PointerHandler* WaylandSeat::FocusHandler() const {
  auto it = pointer_handlers_.find(pointer_focus_);
  return it != pointer_handlers_.end() ? it->second : nullptr;
}

void WaylandSeat::SeatCapabilities(void* data,
                                   wl_seat* seat,
                                   uint32_t capabilities) {
  auto* self = static_cast<WaylandSeat*>(data);
  bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
  if (has_pointer && self->pointer_ == nullptr) {
    self->pointer_ = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(self->pointer_, &kPointerListener, self);
  } else if (!has_pointer && self->pointer_ != nullptr) {
    if (self->cursor_shape_device_ != nullptr) {
      wp_cursor_shape_device_v1_destroy(self->cursor_shape_device_);
      self->cursor_shape_device_ = nullptr;
    }
    wl_pointer_release(self->pointer_);
    self->pointer_ = nullptr;
    self->pointer_focus_ = nullptr;
  }
}

void WaylandSeat::SeatName(void* data, wl_seat* seat, const char* name) {}

void WaylandSeat::PointerEnter(void* data,
                               wl_pointer* pointer,
                               uint32_t serial,
                               wl_surface* surface,
                               wl_fixed_t x,
                               wl_fixed_t y) {
  auto* self = static_cast<WaylandSeat*>(data);
  self->pointer_focus_ = surface;
  self->enter_serial_ = serial;
  PointerHandler* handler = self->FocusHandler();
  if (handler != nullptr) {
    handler->OnPointerEnter(wl_fixed_to_double(x), wl_fixed_to_double(y));
  } else {
    self->SetCursor(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
  }
}

void WaylandSeat::PointerLeave(void* data,
                               wl_pointer* pointer,
                               uint32_t serial,
                               wl_surface* surface) {
  auto* self = static_cast<WaylandSeat*>(data);
  PointerHandler* handler = self->FocusHandler();
  self->pointer_focus_ = nullptr;
  if (handler != nullptr) {
    handler->OnPointerLeave();
  }
}

void WaylandSeat::PointerMotion(void* data,
                                wl_pointer* pointer,
                                uint32_t time,
                                wl_fixed_t x,
                                wl_fixed_t y) {
  auto* self = static_cast<WaylandSeat*>(data);
  PointerHandler* handler = self->FocusHandler();
  if (handler != nullptr) {
    handler->OnPointerMotion(wl_fixed_to_double(x), wl_fixed_to_double(y));
  }
}

void WaylandSeat::PointerButton(void* data,
                                wl_pointer* pointer,
                                uint32_t serial,
                                uint32_t time,
                                uint32_t button,
                                uint32_t state) {
  auto* self = static_cast<WaylandSeat*>(data);
  PointerHandler* handler = self->FocusHandler();
  if (handler != nullptr) {
    handler->OnPointerButton(serial, button,
                             state == WL_POINTER_BUTTON_STATE_PRESSED);
  }
}

void WaylandSeat::PointerAxis(void* data,
                              wl_pointer* pointer,
                              uint32_t time,
                              uint32_t axis,
                              wl_fixed_t value) {}

void WaylandSeat::PointerFrame(void* data, wl_pointer* pointer) {}

void WaylandSeat::PointerAxisSource(void* data,
                                    wl_pointer* pointer,
                                    uint32_t axis_source) {}

void WaylandSeat::PointerAxisStop(void* data,
                                  wl_pointer* pointer,
                                  uint32_t time,
                                  uint32_t axis) {}

void WaylandSeat::PointerAxisDiscrete(void* data,
                                      wl_pointer* pointer,
                                      uint32_t axis,
                                      int32_t discrete) {}

}  // namespace flutter
