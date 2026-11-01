// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland_seat.h"

#include <sys/mman.h>
#include <unistd.h>

#include <chrono>
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

// Converts a time on the event loop clock to microseconds.
uint64_t ToMicros(EventLoop::TimePoint time) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             time.time_since_epoch())
      .count();
}

// Returns the event loop time for a Wayland event time.
EventLoop::TimePoint ToTimePoint(uint32_t time) {
  return EventLoop::TimePoint(std::chrono::microseconds(EventTime(time)));
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

const zwp_pointer_gesture_pinch_v1_listener WaylandSeat::kPinchListener = {
    .begin = PinchBegin,
    .update = PinchUpdate,
    .end = PinchEnd,
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

const wl_keyboard_listener WaylandSeat::kKeyboardListener = {
    .keymap = KeyboardKeymap,
    .enter = KeyboardEnter,
    .leave = KeyboardLeave,
    .key = KeyboardKeyEvent,
    .modifiers = KeyboardModifiers,
    .repeat_info = KeyboardRepeatInfo,
};

WaylandSeat::WaylandSeat(wl_seat* seat,
                         wp_cursor_shape_manager_v1* cursor_shape_manager,
                         zwp_text_input_manager_v3* text_input_manager,
                         zwp_pointer_gestures_v1* pointer_gestures,
                         EventLoop* event_loop)
    : seat_(seat),
      cursor_shape_manager_(cursor_shape_manager),
      pointer_gestures_(pointer_gestures),
      event_loop_(event_loop) {
  wl_seat_add_listener(seat_, &kSeatListener, this);
  if (text_input_manager != nullptr) {
    text_input_ = std::make_unique<WaylandTextInput>(text_input_manager, seat_);
  }
}

WaylandSeat::~WaylandSeat() {
  text_input_.reset();
  ReleaseKeyboard();
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
  if (pinch_focus_ == surface) {
    pinch_focus_ = nullptr;
  }
  for (auto it = touch_focus_.begin(); it != touch_focus_.end();) {
    it = it->second == surface ? touch_focus_.erase(it) : std::next(it);
  }
  touch_frame_surfaces_.erase(surface);
}

void WaylandSeat::SetKeyboardHandler(wl_surface* surface,
                                     KeyboardHandler* handler) {
  if (handler != nullptr) {
    keyboard_handlers_[surface] = handler;
    return;
  }
  keyboard_handlers_.erase(surface);
  if (keyboard_focus_ == surface) {
    keyboard_focus_ = nullptr;
    StopRepeat();
  }
}

void WaylandSeat::SetInputMethodHandler(wl_surface* surface,
                                        InputMethodHandler* handler) {
  if (text_input_ != nullptr) {
    text_input_->SetHandler(surface, handler);
  }
}

void WaylandSeat::SetCursor(uint32_t shape) {
  if (pointer_ == nullptr || pointer_focus_ == nullptr) {
    return;
  }
  if (shape == kHiddenCursorShape) {
    wl_pointer_set_cursor(pointer_, enter_serial_, nullptr, 0, 0);
    return;
  }
  if (cursor_shape_manager_ == nullptr) {
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

KeyboardHandler* WaylandSeat::KeyboardHandlerFor(wl_surface* surface) const {
  auto it = keyboard_handlers_.find(surface);
  return it != keyboard_handlers_.end() ? it->second : nullptr;
}

KeyboardKey WaylandSeat::ResolveKey(uint32_t keycode) {
  return key_resolver_.Resolve(*xkb_keyboard_, keycode);
}

void WaylandSeat::StartRepeat(uint32_t keycode,
                              EventLoop::TimePoint press_time) {
  StopRepeat();
  if (repeat_rate_ <= 0 || !xkb_keyboard_->KeyRepeats(keycode)) {
    return;
  }
  repeat_keycode_ = keycode;
  uint64_t generation = repeat_generation_;
  EventLoop::TimePoint target_time =
      press_time + std::chrono::milliseconds(repeat_delay_);
  event_loop_->PostTask(
      [this, alive = std::weak_ptr<bool>(alive_), generation, target_time]() {
        if (!alive.expired()) {
          RepeatKey(generation, target_time);
        }
      },
      target_time);
}

void WaylandSeat::StopRepeat() {
  repeat_keycode_ = 0;
  repeat_generation_++;
}

void WaylandSeat::RepeatKey(uint64_t generation,
                            EventLoop::TimePoint target_time) {
  if (generation != repeat_generation_ || xkb_keyboard_ == nullptr) {
    return;
  }
  KeyboardHandler* handler = KeyboardHandlerFor(keyboard_focus_);
  if (handler == nullptr) {
    return;
  }
  handler->OnKey(ToMicros(EventLoop::Clock::now()), ResolveKey(repeat_keycode_),
                 KeyAction::kRepeat, xkb_keyboard_->GetModifiers());

  // Schedule from the previous target so the rate doesn't drift, but don't
  // send a burst of repeats if the loop was blocked.
  EventLoop::TimePoint next_time =
      target_time + std::chrono::microseconds(1000000 / repeat_rate_);
  next_time = std::max(next_time, EventLoop::Clock::now());
  event_loop_->PostTask(
      [this, alive = std::weak_ptr<bool>(alive_), generation, next_time]() {
        if (!alive.expired()) {
          RepeatKey(generation, next_time);
        }
      },
      next_time);
}

void WaylandSeat::ReleasePointer() {
  if (pinch_ != nullptr) {
    zwp_pointer_gesture_pinch_v1_destroy(pinch_);
    pinch_ = nullptr;
  }
  pinch_focus_ = nullptr;
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

void WaylandSeat::ReleaseKeyboard() {
  StopRepeat();
  if (keyboard_ != nullptr) {
    wl_keyboard_release(keyboard_);
    keyboard_ = nullptr;
  }
  keyboard_focus_ = nullptr;
  xkb_keyboard_.reset();
  key_resolver_.Reset();
}

void WaylandSeat::SeatCapabilities(void* data,
                                   wl_seat* seat,
                                   uint32_t capabilities) {
  auto* self = static_cast<WaylandSeat*>(data);

  bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
  if (has_pointer && self->pointer_ == nullptr) {
    self->pointer_ = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(self->pointer_, &kPointerListener, self);
    if (self->pointer_gestures_ != nullptr) {
      self->pinch_ = zwp_pointer_gestures_v1_get_pinch_gesture(
          self->pointer_gestures_, self->pointer_);
      zwp_pointer_gesture_pinch_v1_add_listener(self->pinch_, &kPinchListener,
                                                self);
    }
  } else if (!has_pointer && self->pointer_ != nullptr) {
    InputHandler* handler = self->HandlerFor(self->pointer_focus_);
    InputHandler* pinch_handler = self->HandlerFor(self->pinch_focus_);
    self->ReleasePointer();
    uint64_t now = MonotonicMillis() * 1000;
    if (pinch_handler != nullptr) {
      pinch_handler->OnPinchEnd(now, true);
    }
    if (handler != nullptr) {
      handler->OnPointerLeave(now);
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

  bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
  if (has_keyboard && self->keyboard_ == nullptr) {
    self->keyboard_ = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(self->keyboard_, &kKeyboardListener, self);
  } else if (!has_keyboard && self->keyboard_ != nullptr) {
    KeyboardHandler* handler = self->KeyboardHandlerFor(self->keyboard_focus_);
    self->ReleaseKeyboard();
    if (handler != nullptr) {
      handler->OnKeyboardLeave(MonotonicMillis() * 1000);
    }
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
  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    self->last_input_serial_ = serial;
  }
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

void WaylandSeat::PinchBegin(void* data,
                             zwp_pointer_gesture_pinch_v1* pinch,
                             uint32_t serial,
                             uint32_t time,
                             wl_surface* surface,
                             uint32_t fingers) {
  auto* self = static_cast<WaylandSeat*>(data);
  self->pinch_focus_ = surface;
  InputHandler* handler = self->HandlerFor(surface);
  if (handler != nullptr) {
    handler->OnPinchBegin(EventTime(time));
  }
}

void WaylandSeat::PinchUpdate(void* data,
                              zwp_pointer_gesture_pinch_v1* pinch,
                              uint32_t time,
                              wl_fixed_t dx,
                              wl_fixed_t dy,
                              wl_fixed_t scale,
                              wl_fixed_t rotation) {
  auto* self = static_cast<WaylandSeat*>(data);
  InputHandler* handler = self->HandlerFor(self->pinch_focus_);
  if (handler != nullptr) {
    handler->OnPinchUpdate(EventTime(time), wl_fixed_to_double(dx),
                           wl_fixed_to_double(dy), wl_fixed_to_double(scale),
                           wl_fixed_to_double(rotation));
  }
}

void WaylandSeat::PinchEnd(void* data,
                           zwp_pointer_gesture_pinch_v1* pinch,
                           uint32_t serial,
                           uint32_t time,
                           int32_t cancelled) {
  auto* self = static_cast<WaylandSeat*>(data);
  InputHandler* handler = self->HandlerFor(self->pinch_focus_);
  self->pinch_focus_ = nullptr;
  if (handler != nullptr) {
    handler->OnPinchEnd(EventTime(time), cancelled != 0);
  }
}

void WaylandSeat::TouchDown(void* data,
                            wl_touch* touch,
                            uint32_t serial,
                            uint32_t time,
                            wl_surface* surface,
                            int32_t id,
                            wl_fixed_t x,
                            wl_fixed_t y) {
  auto* self = static_cast<WaylandSeat*>(data);
  self->last_input_serial_ = serial;
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

void WaylandSeat::KeyboardKeymap(void* data,
                                 wl_keyboard* keyboard,
                                 uint32_t format,
                                 int32_t fd,
                                 uint32_t size) {
  auto* self = static_cast<WaylandSeat*>(data);
  self->StopRepeat();
  self->key_resolver_.Reset();

  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    close(fd);
    self->xkb_keyboard_.reset();
    return;
  }

  // The keymap must be mapped privately, as it may be shared with other
  // clients.
  void* keymap = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (keymap == MAP_FAILED) {
    self->xkb_keyboard_.reset();
    return;
  }
  self->xkb_keyboard_ = XkbKeyboard::Create(
      std::string_view(static_cast<const char*>(keymap), size));
  munmap(keymap, size);
}

void WaylandSeat::KeyboardEnter(void* data,
                                wl_keyboard* keyboard,
                                uint32_t serial,
                                wl_surface* surface,
                                wl_array* keys) {
  auto* self = static_cast<WaylandSeat*>(data);
  self->keyboard_focus_ = surface;
  self->last_input_serial_ = serial;
  KeyboardHandler* handler = self->KeyboardHandlerFor(surface);
  if (handler == nullptr || self->xkb_keyboard_ == nullptr) {
    return;
  }

  std::vector<KeyboardKey> pressed_keys;
  const auto* codes = static_cast<const uint32_t*>(keys->data);
  for (size_t i = 0; i < keys->size / sizeof(uint32_t); i++) {
    pressed_keys.push_back(self->ResolveKey(codes[i] + 8));
  }
  handler->OnKeyboardEnter(MonotonicMillis() * 1000, pressed_keys,
                           self->xkb_keyboard_->GetModifiers());
}

void WaylandSeat::KeyboardLeave(void* data,
                                wl_keyboard* keyboard,
                                uint32_t serial,
                                wl_surface* surface) {
  auto* self = static_cast<WaylandSeat*>(data);
  self->StopRepeat();
  KeyboardHandler* handler = self->KeyboardHandlerFor(self->keyboard_focus_);
  self->keyboard_focus_ = nullptr;
  if (handler != nullptr) {
    handler->OnKeyboardLeave(MonotonicMillis() * 1000);
  }
}

void WaylandSeat::KeyboardKeyEvent(void* data,
                                   wl_keyboard* keyboard,
                                   uint32_t serial,
                                   uint32_t time,
                                   uint32_t key,
                                   uint32_t state) {
  auto* self = static_cast<WaylandSeat*>(data);
  if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    self->last_input_serial_ = serial;
  }
  KeyboardHandler* handler = self->KeyboardHandlerFor(self->keyboard_focus_);
  if (handler == nullptr || self->xkb_keyboard_ == nullptr) {
    return;
  }

  uint32_t keycode = key + 8;
  bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
  if (pressed) {
    self->StartRepeat(keycode, ToTimePoint(time));
  } else if (keycode == self->repeat_keycode_) {
    self->StopRepeat();
  }

  handler->OnKey(EventTime(time), self->ResolveKey(keycode),
                 pressed ? KeyAction::kDown : KeyAction::kUp,
                 self->xkb_keyboard_->GetModifiers());
}

void WaylandSeat::KeyboardModifiers(void* data,
                                    wl_keyboard* keyboard,
                                    uint32_t serial,
                                    uint32_t mods_depressed,
                                    uint32_t mods_latched,
                                    uint32_t mods_locked,
                                    uint32_t group) {
  auto* self = static_cast<WaylandSeat*>(data);
  if (self->xkb_keyboard_ == nullptr) {
    return;
  }
  self->xkb_keyboard_->UpdateState(mods_depressed, mods_latched, mods_locked,
                                   group);
  KeyboardHandler* handler = self->KeyboardHandlerFor(self->keyboard_focus_);
  if (handler != nullptr) {
    handler->OnKeyModifiers(MonotonicMillis() * 1000,
                            self->xkb_keyboard_->GetModifiers());
  }
}

void WaylandSeat::KeyboardRepeatInfo(void* data,
                                     wl_keyboard* keyboard,
                                     int32_t rate,
                                     int32_t delay) {
  auto* self = static_cast<WaylandSeat*>(data);
  self->repeat_rate_ = rate;
  self->repeat_delay_ = delay;
}

}  // namespace flutter
