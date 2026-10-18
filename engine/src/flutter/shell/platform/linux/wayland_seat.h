// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_SEAT_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_SEAT_H_

#include <wayland-client.h>

#include <cstdint>
#include <map>
#include <memory>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/protocols/cursor-shape-v1-client-protocol.h"

namespace flutter {

// Receives pointer events for a surface. Called on the platform thread.
class PointerHandler {
 public:
  virtual ~PointerHandler() = default;

  // Coordinates are in logical pixels relative to the surface.
  virtual void OnPointerEnter(double x, double y) = 0;
  virtual void OnPointerLeave() = 0;
  virtual void OnPointerMotion(double x, double y) = 0;

  // |button| is a Linux input event code, e.g. BTN_LEFT. |serial| identifies
  // the event in requests such as xdg_toplevel.move.
  virtual void OnPointerButton(uint32_t serial,
                               uint32_t button,
                               bool pressed) = 0;
};

// A group of input devices used by the user.
class WaylandSeat {
 public:
  // Takes ownership of |seat|. |cursor_shape_manager| may be nullptr, in
  // which case the cursor can't be changed.
  WaylandSeat(wl_seat* seat, wp_cursor_shape_manager_v1* cursor_shape_manager);
  ~WaylandSeat();

  wl_seat* seat() const { return seat_; }

  // Sends pointer events over |surface| to |handler|, or stops sending them
  // if |handler| is nullptr.
  void SetPointerHandler(wl_surface* surface, PointerHandler* handler);

  // Sets the cursor shown over the surface the pointer is in. |shape| is a
  // wp_cursor_shape_device_v1 shape.
  void SetCursor(uint32_t shape);

 private:
  static void SeatCapabilities(void* data,
                               wl_seat* seat,
                               uint32_t capabilities);
  static void SeatName(void* data, wl_seat* seat, const char* name);
  static void PointerEnter(void* data,
                           wl_pointer* pointer,
                           uint32_t serial,
                           wl_surface* surface,
                           wl_fixed_t x,
                           wl_fixed_t y);
  static void PointerLeave(void* data,
                           wl_pointer* pointer,
                           uint32_t serial,
                           wl_surface* surface);
  static void PointerMotion(void* data,
                            wl_pointer* pointer,
                            uint32_t time,
                            wl_fixed_t x,
                            wl_fixed_t y);
  static void PointerButton(void* data,
                            wl_pointer* pointer,
                            uint32_t serial,
                            uint32_t time,
                            uint32_t button,
                            uint32_t state);
  static void PointerAxis(void* data,
                          wl_pointer* pointer,
                          uint32_t time,
                          uint32_t axis,
                          wl_fixed_t value);
  static void PointerFrame(void* data, wl_pointer* pointer);
  static void PointerAxisSource(void* data,
                                wl_pointer* pointer,
                                uint32_t axis_source);
  static void PointerAxisStop(void* data,
                              wl_pointer* pointer,
                              uint32_t time,
                              uint32_t axis);
  static void PointerAxisDiscrete(void* data,
                                  wl_pointer* pointer,
                                  uint32_t axis,
                                  int32_t discrete);

  static const wl_seat_listener kSeatListener;
  static const wl_pointer_listener kPointerListener;

  // Returns the handler for the surface the pointer is in, or nullptr.
  PointerHandler* FocusHandler() const;

  wl_seat* seat_;
  wp_cursor_shape_manager_v1* cursor_shape_manager_;
  wl_pointer* pointer_ = nullptr;
  wp_cursor_shape_device_v1* cursor_shape_device_ = nullptr;

  std::map<wl_surface*, PointerHandler*> pointer_handlers_;

  // The surface the pointer is in, and the serial of the enter event.
  wl_surface* pointer_focus_ = nullptr;
  uint32_t enter_serial_ = 0;

  FML_DISALLOW_COPY_AND_ASSIGN(WaylandSeat);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_SEAT_H_
