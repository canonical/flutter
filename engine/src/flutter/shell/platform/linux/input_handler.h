// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_INPUT_HANDLER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_INPUT_HANDLER_H_

#include <cstdint>
#include <vector>

#include "flutter/shell/platform/linux/key_event_converter.h"
// Provides the wl_pointer_axis_source and wp_cursor_shape_device_v1 values
// used by all display backends.
#include "flutter/shell/platform/linux/wayland/protocols/cursor-shape-v1-client-protocol.h"
#include "flutter/shell/platform/linux/wayland/protocols/wayland-client-protocol.h"
#include "flutter/shell/platform/linux/xkb.h"

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

  // A touchpad pinch gesture at the pointer position. |dx| and |dy| are the
  // motion of the fingers' center since the last update, |scale| is the
  // distance between the fingers relative to the start and |rotation| is
  // the clockwise rotation since the last update in degrees.
  virtual void OnPinchBegin(uint64_t time) {}
  virtual void OnPinchUpdate(uint64_t time,
                             double dx,
                             double dy,
                             double scale,
                             double rotation) {}
  virtual void OnPinchEnd(uint64_t time, bool cancelled) {}
};

// Receives keyboard events for a surface. Called on the platform thread.
//
// Times are in microseconds on CLOCK_MONOTONIC. |modifiers| is the state
// before the event.
class KeyboardHandler {
 public:
  virtual ~KeyboardHandler() = default;

  // Called when the surface gains keyboard focus, with the keys already
  // pressed.
  virtual void OnKeyboardEnter(uint64_t time,
                               const std::vector<KeyboardKey>& keys,
                               const KeyModifiers& modifiers) = 0;

  // Called when the surface loses keyboard focus. Pressed keys should be
  // considered released.
  virtual void OnKeyboardLeave(uint64_t time) = 0;

  virtual void OnKey(uint64_t time,
                     const KeyboardKey& key,
                     KeyAction action,
                     const KeyModifiers& modifiers) = 0;

  // Called when the modifier or lock state changes.
  virtual void OnKeyModifiers(uint64_t time, const KeyModifiers& modifiers) = 0;
};

// A cursor shape that hides the cursor.
constexpr uint32_t kHiddenCursorShape = 0;

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_INPUT_HANDLER_H_
