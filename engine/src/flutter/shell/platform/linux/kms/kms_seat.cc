// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/kms/kms_seat.h"

#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <vector>

namespace flutter {

namespace {

// The event loop clock is CLOCK_MONOTONIC, the same as input event times.
uint64_t ToMicros(EventLoop::TimePoint time) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             time.time_since_epoch())
      .count();
}

uint64_t NowMicros() {
  return ToMicros(EventLoop::Clock::now());
}

// Returns the virtual terminal |keysym| switches to, or 0 if none. The
// keymap maps Ctrl+Alt+F<n> to these keysyms.
uint32_t SwitchVtNumber(uint32_t keysym) {
  if (keysym >= XKB_KEY_XF86Switch_VT_1 && keysym <= XKB_KEY_XF86Switch_VT_12) {
    return keysym - XKB_KEY_XF86Switch_VT_1 + 1;
  }
  return 0;
}

}  // namespace

KmsSeat::KmsSeat(EventLoop* event_loop, std::unique_ptr<XkbKeyboard> keyboard)
    : event_loop_(event_loop), keyboard_(std::move(keyboard)) {}

KmsSeat::~KmsSeat() = default;

void KmsSeat::SetHandlers(InputHandler* input_handler,
                          KeyboardHandler* keyboard_handler) {
  if (input_handler == input_handler_ &&
      keyboard_handler == keyboard_handler_) {
    return;
  }
  // Leave the old handlers before switching.
  bool active = active_;
  active_ = false;
  UpdateFocus();
  active_ = active;
  input_handler_ = input_handler;
  keyboard_handler_ = keyboard_handler;
  UpdateFocus();
}

void KmsSeat::SetScreen(int32_t width, int32_t height, double scale) {
  bool first = screen_width_ == 0 && screen_height_ == 0;
  screen_width_ = width;
  screen_height_ = height;
  scale_ = scale;
  if (first) {
    pointer_x_ = width / 2.0;
    pointer_y_ = height / 2.0;
  }
  pointer_x_ = std::clamp(pointer_x_, 0.0, static_cast<double>(width));
  pointer_y_ = std::clamp(pointer_y_, 0.0, static_cast<double>(height));
}

void KmsSeat::SetActive(bool active) {
  if (active_ == active) {
    return;
  }
  active_ = active;
  if (!active_) {
    // Key releases aren't received while in the background, so treat the
    // keys as released. Lock state is kept.
    if (keyboard_ != nullptr) {
      for (uint32_t keycode : pressed_keys_) {
        keyboard_->UpdateKey(keycode, false);
      }
    }
    pressed_keys_.clear();
    consumed_keys_.clear();
  }
  UpdateFocus();
}

bool KmsSeat::focused() const {
  return active_ && (input_handler_ != nullptr || keyboard_handler_ != nullptr);
}

void KmsSeat::UpdateFocus() {
  bool focus = focused();
  if (focus == has_focus_) {
    return;
  }
  uint64_t time = NowMicros();
  if (!focus) {
    StopRepeat();
    if (keyboard_handler_ != nullptr) {
      keyboard_handler_->OnKeyboardLeave(time);
    }
    if (input_handler_ != nullptr) {
      if (!touches_.empty()) {
        input_handler_->OnTouchCancel();
      }
      if (pointer_entered_) {
        input_handler_->OnPointerLeave(time);
      }
    }
    touches_.clear();
    pointer_entered_ = false;
    reported_modifiers_.reset();
    has_focus_ = false;
    return;
  }

  has_focus_ = true;
  if (keyboard_handler_ != nullptr) {
    std::vector<KeyboardKey> keys;
    KeyModifiers modifiers;
    if (keyboard_ != nullptr) {
      for (uint32_t keycode : pressed_keys_) {
        if (consumed_keys_.count(keycode) == 0) {
          keys.push_back(key_resolver_.Resolve(*keyboard_, keycode));
        }
      }
      modifiers = keyboard_->GetModifiers();
    }
    reported_modifiers_ = modifiers;
    keyboard_handler_->OnKeyboardEnter(time, keys, modifiers);
  }
  // The pointer enters when it is next used, as there may not be one.
}

void KmsSeat::ReportModifiers(uint64_t time) {
  if (keyboard_ == nullptr) {
    return;
  }
  KeyModifiers modifiers = keyboard_->GetModifiers();
  if (leds_callback_ &&
      (!led_modifiers_.has_value() ||
       led_modifiers_->caps_lock != modifiers.caps_lock ||
       led_modifiers_->num_lock != modifiers.num_lock ||
       led_modifiers_->scroll_lock != modifiers.scroll_lock)) {
    leds_callback_(modifiers);
  }
  led_modifiers_ = modifiers;

  if (!has_focus_ || keyboard_handler_ == nullptr ||
      reported_modifiers_ == modifiers) {
    return;
  }
  reported_modifiers_ = modifiers;
  keyboard_handler_->OnKeyModifiers(time, modifiers);
}

void KmsSeat::OnKmsKey(uint64_t time, uint32_t code, bool pressed) {
  if (keyboard_ == nullptr) {
    return;
  }
  // XKB keycodes are offset from Linux input event codes.
  uint32_t keycode = code + 8;
  KeyboardKey key = key_resolver_.Resolve(*keyboard_, keycode);
  KeyModifiers modifiers = keyboard_->GetModifiers();
  bool send = has_focus_ && keyboard_handler_ != nullptr;

  if (pressed) {
    if (!pressed_keys_.insert(keycode).second) {
      return;
    }
    keyboard_->UpdateKey(keycode, true);
    uint32_t vt = SwitchVtNumber(key.keysym);
    if (vt != 0) {
      consumed_keys_.insert(keycode);
      StopRepeat();
      if (switch_vt_callback_) {
        switch_vt_callback_(vt);
      }
    } else if (send) {
      reported_modifiers_ = modifiers;
      keyboard_handler_->OnKey(time, key, KeyAction::kDown, modifiers);
      StartRepeat(keycode);
    }
  } else {
    if (pressed_keys_.erase(keycode) == 0) {
      return;
    }
    keyboard_->UpdateKey(keycode, false);
    if (repeat_keycode_ == keycode) {
      StopRepeat();
    }
    if (consumed_keys_.erase(keycode) == 0 && send) {
      reported_modifiers_ = modifiers;
      keyboard_handler_->OnKey(time, key, KeyAction::kUp, modifiers);
    }
  }
  ReportModifiers(time);
}

void KmsSeat::StartRepeat(uint32_t keycode) {
  StopRepeat();
  if (!keyboard_->KeyRepeats(keycode)) {
    return;
  }
  repeat_keycode_ = keycode;
  uint64_t generation = repeat_generation_;
  EventLoop::TimePoint target_time =
      EventLoop::Clock::now() + std::chrono::milliseconds(kRepeatDelayMs);
  event_loop_->PostTask(
      [this, alive = std::weak_ptr<bool>(alive_), generation, target_time]() {
        if (!alive.expired()) {
          RepeatKey(generation, target_time);
        }
      },
      target_time);
}

void KmsSeat::StopRepeat() {
  repeat_keycode_ = 0;
  repeat_generation_++;
}

void KmsSeat::RepeatKey(uint64_t generation, EventLoop::TimePoint target_time) {
  if (generation != repeat_generation_ || !has_focus_ ||
      keyboard_handler_ == nullptr) {
    return;
  }
  KeyModifiers modifiers = keyboard_->GetModifiers();
  reported_modifiers_ = modifiers;
  keyboard_handler_->OnKey(NowMicros(),
                           key_resolver_.Resolve(*keyboard_, repeat_keycode_),
                           KeyAction::kRepeat, modifiers);

  // Schedule from the previous target so the rate doesn't drift, but don't
  // send a burst of repeats if the loop was blocked.
  EventLoop::TimePoint next_time =
      std::max(target_time + std::chrono::microseconds(1000000 / kRepeatRate),
               EventLoop::Clock::now());
  event_loop_->PostTask(
      [this, alive = std::weak_ptr<bool>(alive_), generation, next_time]() {
        if (!alive.expired()) {
          RepeatKey(generation, next_time);
        }
      },
      next_time);
}

bool KmsSeat::EnsurePointerEntered(uint64_t time) {
  if (!has_focus_ || input_handler_ == nullptr) {
    return false;
  }
  if (!pointer_entered_) {
    pointer_entered_ = true;
    input_handler_->OnPointerEnter(time, pointer_x_ / scale_,
                                   pointer_y_ / scale_);
  }
  return true;
}

void KmsSeat::MovePointer(uint64_t time, double x, double y) {
  pointer_x_ = std::clamp(x, 0.0, static_cast<double>(screen_width_));
  pointer_y_ = std::clamp(y, 0.0, static_cast<double>(screen_height_));
  if (pointer_callback_) {
    pointer_callback_();
  }
  bool entered = pointer_entered_;
  if (EnsurePointerEntered(time) && entered) {
    input_handler_->OnPointerMotion(time, pointer_x_ / scale_,
                                    pointer_y_ / scale_);
  }
}

void KmsSeat::OnKmsPointerMotion(uint64_t time, double dx, double dy) {
  MovePointer(time, pointer_x_ + dx, pointer_y_ + dy);
}

void KmsSeat::OnKmsPointerMotionAbsolute(uint64_t time, double x, double y) {
  MovePointer(time, x, y);
}

void KmsSeat::OnKmsPointerButton(uint64_t time, uint32_t button, bool pressed) {
  if (EnsurePointerEntered(time)) {
    input_handler_->OnPointerButton(++button_serial_, time, button, pressed);
  }
}

void KmsSeat::OnKmsPointerScroll(uint64_t time, const PointerScroll& scroll) {
  if (EnsurePointerEntered(time)) {
    input_handler_->OnPointerScroll(time, scroll);
  }
}

void KmsSeat::OnKmsTouchDown(uint64_t time, int32_t id, double x, double y) {
  if (!has_focus_ || input_handler_ == nullptr) {
    return;
  }
  touches_.insert(id);
  input_handler_->OnTouchDown(++button_serial_, time, id, x / scale_,
                              y / scale_);
}

void KmsSeat::OnKmsTouchUp(uint64_t time, int32_t id) {
  if (touches_.erase(id) > 0 && input_handler_ != nullptr) {
    input_handler_->OnTouchUp(time, id);
  }
}

void KmsSeat::OnKmsTouchMotion(uint64_t time, int32_t id, double x, double y) {
  if (touches_.count(id) > 0 && input_handler_ != nullptr) {
    input_handler_->OnTouchMotion(time, id, x / scale_, y / scale_);
  }
}

void KmsSeat::OnKmsTouchCancel() {
  if (!touches_.empty() && input_handler_ != nullptr) {
    input_handler_->OnTouchCancel();
  }
  touches_.clear();
}

void KmsSeat::OnKmsTouchFrame() {
  if (has_focus_ && input_handler_ != nullptr) {
    input_handler_->OnTouchFrame();
  }
}

void KmsSeat::OnKmsPinchBegin(uint64_t time) {
  if (EnsurePointerEntered(time)) {
    input_handler_->OnPinchBegin(time);
  }
}

void KmsSeat::OnKmsPinchUpdate(uint64_t time,
                               double dx,
                               double dy,
                               double scale,
                               double rotation) {
  if (EnsurePointerEntered(time)) {
    input_handler_->OnPinchUpdate(time, dx / scale_, dy / scale_, scale,
                                  rotation);
  }
}

void KmsSeat::OnKmsPinchEnd(uint64_t time, bool cancelled) {
  if (EnsurePointerEntered(time)) {
    input_handler_->OnPinchEnd(time, cancelled);
  }
}

void KmsSeat::OnKmsPointerAvailable(bool available) {
  has_pointer_ = available;
  if (!available && pointer_entered_ && input_handler_ != nullptr) {
    input_handler_->OnPointerLeave(NowMicros());
  }
  if (!available) {
    pointer_entered_ = false;
  }
  if (pointer_callback_) {
    pointer_callback_();
  }
}

}  // namespace flutter
