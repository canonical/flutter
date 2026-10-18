// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland_seat.h"

#include <ctime>

namespace flutter {

namespace {

uint64_t MonotonicMillis() {
  timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);
  return static_cast<uint64_t>(time.tv_sec) * 1000 + time.tv_nsec / 1000000;
}

// Converts a Wayland event time, which is in milliseconds and wraps every 49
// days, to microseconds on CLOCK_MONOTONIC. Compositors use CLOCK_MONOTONIC
// for event times, but if this one doesn't the current time is used.
uint64_t EventTime(uint32_t time) {
  constexpr uint32_t kMaxEventAge = 10000;
  uint64_t now = MonotonicMillis();
  uint32_t age = static_cast<uint32_t>(now) - time;
  if (age > kMaxEventAge) {
    return now * 1000;
  }
  return (now - age) * 1000;
}

}  // namespace

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

const wl_touch_listener WaylandSeat::kTouchListener = {
    .down = TouchDown,
    .up = TouchUp,
    .motion = TouchMotion,
    .frame = TouchFrame,
    .cancel = TouchCancel,
    .shape = TouchShape,
    .orientation = TouchOrientation,
};

WaylandSeat::WaylandSeat(wl_seat* seat,
                         wp_cursor_shape_manager_v1* cursor_shape_manager)
    : seat_(seat), cursor_shape_manager_(cursor_shape_manager) {
  wl_seat_add_listener(seat_, &kSeatListener, this);
}

WaylandSeat::~WaylandSeat() {
  ReleaseTouch();
  ReleasePointer();
  wl_seat_destroy(seat_);
}

void WaylandSeat::SetInputHandler(wl_surface* surface, InputHandler* handler) {
  if (handler != nullptr) {
    handlers_[surface] = handler;
    return;
  }
  handlers_.erase(surface);
  if (pointer_focus_ == surface) {
    pointer_focus_ = nullptr;
    has_scroll_ = false;
  }
  for (auto it = touch_focus_.begin(); it != touch_focus_.end();) {
    it = it->second == surface ? touch_focus_.erase(it) : std::next(it);
  }
  touch_frame_surfaces_.erase(surface);
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

InputHandler* WaylandSeat::HandlerFor(wl_surface* surface) const {
  auto it = handlers_.find(surface);
  return it != handlers_.end() ? it->second : nullptr;
}

void WaylandSeat::ReleasePointer() {
  if (cursor_shape_device_ != nullptr) {
    wp_cursor_shape_device_v1_destroy(cursor_shape_device_);
    cursor_shape_device_ = nullptr;
  }
  if (pointer_ != nullptr) {
    wl_pointer_release(pointer_);
    pointer_ = nullptr;
  }
  pointer_focus_ = nullptr;
  has_scroll_ = false;
}

void WaylandSeat::ReleaseTouch() {
  if (touch_ != nullptr) {
    wl_touch_release(touch_);
    touch_ = nullptr;
  }
  touch_focus_.clear();
  touch_frame_surfaces_.clear();
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
    InputHandler* handler = self->HandlerFor(self->pointer_focus_);
    self->ReleasePointer();
    if (handler != nullptr) {
      handler->OnPointerLeave(MonotonicMillis() * 1000);
    }
  }

  bool has_touch = (capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0;
  if (has_touch && self->touch_ == nullptr) {
    self->touch_ = wl_seat_get_touch(seat);
    wl_touch_add_listener(self->touch_, &kTouchListener, self);
  } else if (!has_touch && self->touch_ != nullptr) {
    TouchCancel(self, self->touch_);
    self->ReleaseTouch();
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
  self->has_scroll_ = false;
  InputHandler* handler = self->HandlerFor(surface);
  if (handler != nullptr) {
    handler->OnPointerEnter(MonotonicMillis() * 1000, wl_fixed_to_double(x),
                            wl_fixed_to_double(y));
  } else {
    self->SetCursor(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
  }
}

void WaylandSeat::PointerLeave(void* data,
                               wl_pointer* pointer,
                               uint32_t serial,
                               wl_surface* surface) {
  auto* self = static_cast<WaylandSeat*>(data);
  self->FlushScroll();
  InputHandler* handler = self->HandlerFor(self->pointer_focus_);
  self->pointer_focus_ = nullptr;
  if (handler != nullptr) {
    handler->OnPointerLeave(MonotonicMillis() * 1000);
  }
}

void WaylandSeat::PointerMotion(void* data,
                                wl_pointer* pointer,
                                uint32_t time,
                                wl_fixed_t x,
                                wl_fixed_t y) {
  auto* self = static_cast<WaylandSeat*>(data);
  InputHandler* handler = self->HandlerFor(self->pointer_focus_);
  if (handler != nullptr) {
    handler->OnPointerMotion(EventTime(time), wl_fixed_to_double(x),
                             wl_fixed_to_double(y));
  }
}

void WaylandSeat::PointerButton(void* data,
                                wl_pointer* pointer,
                                uint32_t serial,
                                uint32_t time,
                                uint32_t button,
                                uint32_t state) {
  auto* self = static_cast<WaylandSeat*>(data);
  InputHandler* handler = self->HandlerFor(self->pointer_focus_);
  if (handler != nullptr) {
    handler->OnPointerButton(serial, EventTime(time), button,
                             state == WL_POINTER_BUTTON_STATE_PRESSED);
  }
}

void WaylandSeat::AddScroll(uint32_t time) {
  if (!has_scroll_) {
    has_scroll_ = true;
    scroll_ = PointerScroll();
  }
  scroll_time_ = EventTime(time);
}

void WaylandSeat::FlushScroll() {
  if (!has_scroll_) {
    return;
  }
  has_scroll_ = false;
  InputHandler* handler = HandlerFor(pointer_focus_);
  if (handler != nullptr) {
    handler->OnPointerScroll(scroll_time_, scroll_);
  }
}

void WaylandSeat::PointerAxis(void* data,
                              wl_pointer* pointer,
                              uint32_t time,
                              uint32_t axis,
                              wl_fixed_t value) {
  auto* self = static_cast<WaylandSeat*>(data);
  self->AddScroll(time);
  if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
    self->scroll_.dx += wl_fixed_to_double(value);
  } else {
    self->scroll_.dy += wl_fixed_to_double(value);
  }
  // Before version 5 events aren't grouped into frames.
  if (wl_pointer_get_version(pointer) < WL_POINTER_FRAME_SINCE_VERSION) {
    self->FlushScroll();
  }
}

void WaylandSeat::PointerFrame(void* data, wl_pointer* pointer) {
  static_cast<WaylandSeat*>(data)->FlushScroll();
}

void WaylandSeat::PointerAxisSource(void* data,
                                    wl_pointer* pointer,
                                    uint32_t axis_source) {
  auto* self = static_cast<WaylandSeat*>(data);
  if (!self->has_scroll_) {
    self->has_scroll_ = true;
    self->scroll_ = PointerScroll();
    self->scroll_time_ = MonotonicMillis() * 1000;
  }
  self->scroll_.source = axis_source;
}

void WaylandSeat::PointerAxisStop(void* data,
                                  wl_pointer* pointer,
                                  uint32_t time,
                                  uint32_t axis) {
  auto* self = static_cast<WaylandSeat*>(data);
  self->AddScroll(time);
  self->scroll_.stop = true;
}

// The distance in PointerAxis is used instead, as it is also provided for
// high resolution wheels.
void WaylandSeat::PointerAxisDiscrete(void* data,
                                      wl_pointer* pointer,
                                      uint32_t axis,
                                      int32_t discrete) {}

void WaylandSeat::TouchDown(void* data,
                            wl_touch* touch,
                            uint32_t serial,
                            uint32_t time,
                            wl_surface* surface,
                            int32_t id,
                            wl_fixed_t x,
                            wl_fixed_t y) {
  auto* self = static_cast<WaylandSeat*>(data);
  InputHandler* handler = self->HandlerFor(surface);
  if (handler == nullptr) {
    return;
  }
  self->touch_focus_[id] = surface;
  self->touch_frame_surfaces_.insert(surface);
  handler->OnTouchDown(serial, EventTime(time), id, wl_fixed_to_double(x),
                       wl_fixed_to_double(y));
}

void WaylandSeat::TouchUp(void* data,
                          wl_touch* touch,
                          uint32_t serial,
                          uint32_t time,
                          int32_t id) {
  auto* self = static_cast<WaylandSeat*>(data);
  auto it = self->touch_focus_.find(id);
  if (it == self->touch_focus_.end()) {
    return;
  }
  wl_surface* surface = it->second;
  self->touch_focus_.erase(it);
  self->touch_frame_surfaces_.insert(surface);
  InputHandler* handler = self->HandlerFor(surface);
  if (handler != nullptr) {
    handler->OnTouchUp(EventTime(time), id);
  }
}

void WaylandSeat::TouchMotion(void* data,
                              wl_touch* touch,
                              uint32_t time,
                              int32_t id,
                              wl_fixed_t x,
                              wl_fixed_t y) {
  auto* self = static_cast<WaylandSeat*>(data);
  auto it = self->touch_focus_.find(id);
  if (it == self->touch_focus_.end()) {
    return;
  }
  self->touch_frame_surfaces_.insert(it->second);
  InputHandler* handler = self->HandlerFor(it->second);
  if (handler != nullptr) {
    handler->OnTouchMotion(EventTime(time), id, wl_fixed_to_double(x),
                           wl_fixed_to_double(y));
  }
}

void WaylandSeat::TouchFrame(void* data, wl_touch* touch) {
  auto* self = static_cast<WaylandSeat*>(data);
  std::set<wl_surface*> surfaces;
  surfaces.swap(self->touch_frame_surfaces_);
  for (wl_surface* surface : surfaces) {
    InputHandler* handler = self->HandlerFor(surface);
    if (handler != nullptr) {
      handler->OnTouchFrame();
    }
  }
}

void WaylandSeat::TouchCancel(void* data, wl_touch* touch) {
  auto* self = static_cast<WaylandSeat*>(data);
  std::set<wl_surface*> surfaces = self->touch_frame_surfaces_;
  for (const auto& [id, surface] : self->touch_focus_) {
    surfaces.insert(surface);
  }
  self->touch_focus_.clear();
  self->touch_frame_surfaces_.clear();
  for (wl_surface* surface : surfaces) {
    InputHandler* handler = self->HandlerFor(surface);
    if (handler != nullptr) {
      handler->OnTouchCancel();
    }
  }
}

void WaylandSeat::TouchShape(void* data,
                             wl_touch* touch,
                             int32_t id,
                             wl_fixed_t major,
                             wl_fixed_t minor) {}

void WaylandSeat::TouchOrientation(void* data,
                                   wl_touch* touch,
                                   int32_t id,
                                   wl_fixed_t orientation) {}

}  // namespace flutter
