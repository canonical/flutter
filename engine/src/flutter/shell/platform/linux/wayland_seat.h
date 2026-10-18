// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_SEAT_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_SEAT_H_

#include <cstdint>
#include <map>
#include <set>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/protocols/cursor-shape-v1-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/wayland-client-protocol.h"

namespace flutter {

// Scrolling from a single pointer frame.
struct PointerScroll {
  // A wl_pointer_axis_source value, e.g. WL_POINTER_AXIS_SOURCE_WHEEL.
  uint32_t source = WL_POINTER_AXIS_SOURCE_WHEEL;

  // The distance scrolled in surface coordinates. Positive values scroll
  // right and down.
  double dx = 0;
  double dy = 0;

  // True if scrolling stopped, e.g. the fingers were lifted from a touchpad.
  bool stop = false;
};

// Receives input events for a surface. Called on the platform thread.
//
// Coordinates are in logical pixels relative to the surface. Times are in
// microseconds on CLOCK_MONOTONIC. Serials identify events in requests such
// as xdg_toplevel.move.
class InputHandler {
 public:
  virtual ~InputHandler() = default;

  virtual void OnPointerEnter(uint64_t time, double x, double y) = 0;
  virtual void OnPointerLeave(uint64_t time) = 0;
  virtual void OnPointerMotion(uint64_t time, double x, double y) = 0;

  // |button| is a Linux input event code, e.g. BTN_LEFT.
  virtual void OnPointerButton(uint32_t serial,
                               uint64_t time,
                               uint32_t button,
                               bool pressed) = 0;

  virtual void OnPointerScroll(uint64_t time, const PointerScroll& scroll) {}

  // |id| identifies a touch point until it is lifted.
  virtual void OnTouchDown(uint32_t serial,
                           uint64_t time,
                           int32_t id,
                           double x,
                           double y) {}
  virtual void OnTouchUp(uint64_t time, int32_t id) {}
  virtual void OnTouchMotion(uint64_t time, int32_t id, double x, double y) {}

  // Called when the compositor takes over all touch points, e.g. for a
  // gesture. No more events are sent for current touch points.
  virtual void OnTouchCancel() {}

  // Called after a group of touch events that happened at the same time.
  virtual void OnTouchFrame() {}
};

// A group of input devices used by the user.
class WaylandSeat {
 public:
  // Takes ownership of |seat|. |cursor_shape_manager| may be nullptr, in
  // which case the cursor can't be changed.
  WaylandSeat(wl_seat* seat, wp_cursor_shape_manager_v1* cursor_shape_manager);
  ~WaylandSeat();

  wl_seat* seat() const { return seat_; }

  // Sends input events over |surface| to |handler|, or stops sending them if
  // |handler| is nullptr.
  void SetInputHandler(wl_surface* surface, InputHandler* handler);

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
  static void TouchDown(void* data,
                        wl_touch* touch,
                        uint32_t serial,
                        uint32_t time,
                        wl_surface* surface,
                        int32_t id,
                        wl_fixed_t x,
                        wl_fixed_t y);
  static void TouchUp(void* data,
                      wl_touch* touch,
                      uint32_t serial,
                      uint32_t time,
                      int32_t id);
  static void TouchMotion(void* data,
                          wl_touch* touch,
                          uint32_t time,
                          int32_t id,
                          wl_fixed_t x,
                          wl_fixed_t y);
  static void TouchFrame(void* data, wl_touch* touch);
  static void TouchCancel(void* data, wl_touch* touch);
  static void TouchShape(void* data,
                         wl_touch* touch,
                         int32_t id,
                         wl_fixed_t major,
                         wl_fixed_t minor);
  static void TouchOrientation(void* data,
                               wl_touch* touch,
                               int32_t id,
                               wl_fixed_t orientation);

  static const wl_seat_listener kSeatListener;
  static const wl_pointer_listener kPointerListener;
  static const wl_touch_listener kTouchListener;

  // Returns the handler for |surface|, or nullptr.
  InputHandler* HandlerFor(wl_surface* surface) const;

  // Records |time| as the time of the scroll in the current frame.
  void AddScroll(uint32_t time);

  // Sends the scrolling accumulated in the current pointer frame.
  void FlushScroll();

  void ReleasePointer();
  void ReleaseTouch();

  wl_seat* seat_;
  wp_cursor_shape_manager_v1* cursor_shape_manager_;
  wl_pointer* pointer_ = nullptr;
  wp_cursor_shape_device_v1* cursor_shape_device_ = nullptr;
  wl_touch* touch_ = nullptr;

  std::map<wl_surface*, InputHandler*> handlers_;

  // The surface the pointer is in, and the serial of the enter event.
  wl_surface* pointer_focus_ = nullptr;
  uint32_t enter_serial_ = 0;

  // Scrolling in the current pointer frame.
  bool has_scroll_ = false;
  uint64_t scroll_time_ = 0;
  PointerScroll scroll_;

  // The surface each touch point started in.
  std::map<int32_t, wl_surface*> touch_focus_;

  // Surfaces that have received touch events in the current frame.
  std::set<wl_surface*> touch_frame_surfaces_;

  FML_DISALLOW_COPY_AND_ASSIGN(WaylandSeat);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_SEAT_H_
