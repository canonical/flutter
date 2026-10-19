// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_SEAT_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_SEAT_H_

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/key_event_converter.h"
#include "flutter/shell/platform/linux/protocols/cursor-shape-v1-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/wayland-client-protocol.h"
#include "flutter/shell/platform/linux/wayland_text_input.h"
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

// A group of input devices used by the user.
class WaylandSeat {
 public:
  // Takes ownership of |seat|. |cursor_shape_manager| may be nullptr, in
  // which case the cursor can't be changed. |text_input_manager| may be
  // nullptr, in which case there is no input method. |event_loop| is used to
  // repeat keys and must outlive the seat.
  WaylandSeat(wl_seat* seat,
              wp_cursor_shape_manager_v1* cursor_shape_manager,
              zwp_text_input_manager_v3* text_input_manager,
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

  // Returns the logical keys chosen to match the US layout for |layout|.
  const std::unordered_map<uint32_t, uint64_t>& LayoutLogicalKeys(
      uint32_t layout);

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
  EventLoop* event_loop_;
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

  wl_keyboard* keyboard_ = nullptr;
  std::map<wl_surface*, KeyboardHandler*> keyboard_handlers_;

  // The keymap and state, or nullptr if no keymap has been received.
  std::unique_ptr<XkbKeyboard> xkb_keyboard_;

  // Logical keys derived from each layout of the keymap.
  std::map<uint32_t, std::unordered_map<uint32_t, uint64_t>> layout_keys_;

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

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_SEAT_H_
