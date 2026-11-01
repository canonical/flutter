// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_SEAT_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_SEAT_H_

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/input_handler.h"
#include "flutter/shell/platform/linux/key_event_converter.h"
#include "flutter/shell/platform/linux/wayland/protocols/cursor-shape-v1-client-protocol.h"
#include "flutter/shell/platform/linux/wayland/protocols/pointer-gestures-unstable-v1-client-protocol.h"
#include "flutter/shell/platform/linux/wayland/protocols/wayland-client-protocol.h"
#include "flutter/shell/platform/linux/wayland/wayland_text_input.h"
#include "flutter/shell/platform/linux/xkb.h"

namespace flutter {

// A group of input devices used by the user.
class WaylandSeat {
 public:
  // Takes ownership of |seat|. |cursor_shape_manager| may be nullptr, in
  // which case the cursor can't be changed. |text_input_manager| may be
  // nullptr, in which case there is no input method. |pointer_gestures| may
  // be nullptr, in which case there are no touchpad gestures. |event_loop| is
  // used to repeat keys and must outlive the seat.
  WaylandSeat(wl_seat* seat,
              wp_cursor_shape_manager_v1* cursor_shape_manager,
              zwp_text_input_manager_v3* text_input_manager,
              zwp_pointer_gestures_v1* pointer_gestures,
              EventLoop* event_loop);
  ~WaylandSeat();

  wl_seat* seat() const { return seat_; }

  // Sends input events over |surface| to |handler|, or stops sending them if
  // |handler| is nullptr.
  void SetInputHandler(wl_surface* surface, InputHandler* handler);

  // Sends keyboard events for |surface| to |handler|, or stops sending them
  // if |handler| is nullptr.
  void SetKeyboardHandler(wl_surface* surface, KeyboardHandler* handler);

  // Sends input method requests for |surface| to |handler|, or stops sending
  // them if |handler| is nullptr.
  void SetInputMethodHandler(wl_surface* surface, InputMethodHandler* handler);

  // The surface the pointer is in, or nullptr.
  wl_surface* pointer_focus() const { return pointer_focus_; }

  // The surface with keyboard focus, or nullptr.
  wl_surface* keyboard_focus() const { return keyboard_focus_; }

  // The serial of the most recent user input (button press, key press, touch
  // or keyboard focus), or zero if there has been none. Compositors use this
  // to verify requests made in response to user input, such as activation.
  uint32_t last_input_serial() const { return last_input_serial_; }

  // Sets the cursor shown over the surface the pointer is in. |shape| is a
  // wp_cursor_shape_device_v1 shape, or kHiddenCursorShape.
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
  static void PinchBegin(void* data,
                         zwp_pointer_gesture_pinch_v1* pinch,
                         uint32_t serial,
                         uint32_t time,
                         wl_surface* surface,
                         uint32_t fingers);
  static void PinchUpdate(void* data,
                          zwp_pointer_gesture_pinch_v1* pinch,
                          uint32_t time,
                          wl_fixed_t dx,
                          wl_fixed_t dy,
                          wl_fixed_t scale,
                          wl_fixed_t rotation);
  static void PinchEnd(void* data,
                       zwp_pointer_gesture_pinch_v1* pinch,
                       uint32_t serial,
                       uint32_t time,
                       int32_t cancelled);
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

  static void KeyboardKeymap(void* data,
                             wl_keyboard* keyboard,
                             uint32_t format,
                             int32_t fd,
                             uint32_t size);
  static void KeyboardEnter(void* data,
                            wl_keyboard* keyboard,
                            uint32_t serial,
                            wl_surface* surface,
                            wl_array* keys);
  static void KeyboardLeave(void* data,
                            wl_keyboard* keyboard,
                            uint32_t serial,
                            wl_surface* surface);
  static void KeyboardKeyEvent(void* data,
                               wl_keyboard* keyboard,
                               uint32_t serial,
                               uint32_t time,
                               uint32_t key,
                               uint32_t state);
  static void KeyboardModifiers(void* data,
                                wl_keyboard* keyboard,
                                uint32_t serial,
                                uint32_t mods_depressed,
                                uint32_t mods_latched,
                                uint32_t mods_locked,
                                uint32_t group);
  static void KeyboardRepeatInfo(void* data,
                                 wl_keyboard* keyboard,
                                 int32_t rate,
                                 int32_t delay);

  static const wl_seat_listener kSeatListener;
  static const wl_pointer_listener kPointerListener;
  static const zwp_pointer_gesture_pinch_v1_listener kPinchListener;
  static const wl_touch_listener kTouchListener;
  static const wl_keyboard_listener kKeyboardListener;

  // Returns the handler for |surface|, or nullptr.
  InputHandler* HandlerFor(wl_surface* surface) const;

  // Records |time| as the time of the scroll in the current frame.
  void AddScroll(uint32_t time);

  // Sends the scrolling accumulated in the current pointer frame.
  void FlushScroll();

  // Returns the keyboard handler for |surface|, or nullptr.
  KeyboardHandler* KeyboardHandlerFor(wl_surface* surface) const;

  // Returns the key |keycode| produces in the current keyboard state.
  KeyboardKey ResolveKey(uint32_t keycode);

  // Starts repeating |keycode| if it repeats, replacing any repeating key.
  void StartRepeat(uint32_t keycode, EventLoop::TimePoint press_time);
  void StopRepeat();

  // Sends a repeat of the repeating key if |generation| is still current,
  // and schedules the next one.
  void RepeatKey(uint64_t generation, EventLoop::TimePoint target_time);

  void ReleasePointer();
  void ReleaseTouch();
  void ReleaseKeyboard();

  wl_seat* seat_;
  wp_cursor_shape_manager_v1* cursor_shape_manager_;
  zwp_pointer_gestures_v1* pointer_gestures_;
  EventLoop* event_loop_;
  wl_pointer* pointer_ = nullptr;
  wp_cursor_shape_device_v1* cursor_shape_device_ = nullptr;
  zwp_pointer_gesture_pinch_v1* pinch_ = nullptr;
  // The surface the pinch gesture in progress started in.
  wl_surface* pinch_focus_ = nullptr;
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

  wl_keyboard* keyboard_ = nullptr;
  std::map<wl_surface*, KeyboardHandler*> keyboard_handlers_;

  // The keymap and state, or nullptr if no keymap has been received.
  std::unique_ptr<XkbKeyboard> xkb_keyboard_;

  KeyResolver key_resolver_;

  // The surface with keyboard focus.
  wl_surface* keyboard_focus_ = nullptr;

  uint32_t last_input_serial_ = 0;

  // Key repeat settings, as keys per second and milliseconds before the
  // first repeat. A rate of zero disables repeating.
  int32_t repeat_rate_ = 25;
  int32_t repeat_delay_ = 600;

  // The key being repeated, and a counter that invalidates pending repeat
  // tasks when it changes.
  uint32_t repeat_keycode_ = 0;
  uint64_t repeat_generation_ = 0;

  // The input method, or nullptr if the compositor doesn't support one.
  std::unique_ptr<WaylandTextInput> text_input_;

  // Repeat tasks hold a weak reference to this, so they don't run after the
  // seat is destroyed.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(WaylandSeat);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_SEAT_H_
