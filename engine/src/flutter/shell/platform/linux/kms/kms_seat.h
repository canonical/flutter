// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_SEAT_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_SEAT_H_

#include <functional>
#include <memory>
#include <optional>
#include <set>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/input_handler.h"
#include "flutter/shell/platform/linux/key_event_converter.h"
#include "flutter/shell/platform/linux/kms/kms_input.h"
#include "flutter/shell/platform/linux/xkb.h"

namespace flutter {

// Turns input device events into events for the window on the screen.
//
// There is no compositor, so the seat keeps the pointer position, the
// keyboard state and key repeat itself. Input goes to the window while it
// has handlers and the session is active.
class KmsSeat : public KmsInput::Delegate {
 public:
  static constexpr int kRepeatDelayMs = 600;
  static constexpr int kRepeatRate = 25;

  // |keyboard| may be nullptr if no keymap could be loaded, in which case
  // key events are ignored.
  KmsSeat(EventLoop* event_loop, std::unique_ptr<XkbKeyboard> keyboard);
  ~KmsSeat() override;

  // Sets the handlers for the window, or nullptr to stop sending input.
  void SetHandlers(InputHandler* input_handler,
                   KeyboardHandler* keyboard_handler);

  // Sets the size of the screen in physical pixels and the scale to convert
  // to window coordinates.
  void SetScreen(int32_t width, int32_t height, double scale);

  // Sets if the session is in the foreground. Input stops while it isn't.
  void SetActive(bool active);

  // Called when Ctrl+Alt+F<n> is pressed, to switch virtual terminal.
  void set_switch_vt_callback(std::function<void(uint32_t vt)> callback) {
    switch_vt_callback_ = std::move(callback);
  }

  // Called when the pointer moves or a pointer device is added or removed.
  void set_pointer_callback(std::function<void()> callback) {
    pointer_callback_ = std::move(callback);
  }

  // Called when the lock state changes, to update the keyboard LEDs.
  void set_leds_callback(
      std::function<void(const KeyModifiers& modifiers)> callback) {
    leds_callback_ = std::move(callback);
  }

  // The pointer position in physical pixels.
  double pointer_x() const { return pointer_x_; }
  double pointer_y() const { return pointer_y_; }

  // True if there is a pointer device.
  bool has_pointer() const { return has_pointer_; }

  // True if the window is receiving pointer or keyboard input.
  bool pointer_focused() const { return pointer_entered_; }
  bool keyboard_focused() const {
    return has_focus_ && keyboard_handler_ != nullptr;
  }

  // KmsInput::Delegate
  void OnKmsKey(uint64_t time, uint32_t code, bool pressed) override;
  void OnKmsPointerMotion(uint64_t time, double dx, double dy) override;
  void OnKmsPointerMotionAbsolute(uint64_t time, double x, double y) override;
  void OnKmsPointerButton(uint64_t time,
                          uint32_t button,
                          bool pressed) override;
  void OnKmsPointerScroll(uint64_t time, const PointerScroll& scroll) override;
  void OnKmsTouchDown(uint64_t time, int32_t id, double x, double y) override;
  void OnKmsTouchUp(uint64_t time, int32_t id) override;
  void OnKmsTouchMotion(uint64_t time, int32_t id, double x, double y) override;
  void OnKmsTouchCancel() override;
  void OnKmsTouchFrame() override;
  void OnKmsPinchBegin(uint64_t time) override;
  void OnKmsPinchUpdate(uint64_t time,
                        double dx,
                        double dy,
                        double scale,
                        double rotation) override;
  void OnKmsPinchEnd(uint64_t time, bool cancelled) override;
  void OnKmsPointerAvailable(bool available) override;

 private:
  bool focused() const;
  // Sends enter or leave events if the focus changed.
  void UpdateFocus();
  // Sends a pointer enter if the pointer isn't in the window yet. Returns
  // false if pointer events can't be sent.
  bool EnsurePointerEntered(uint64_t time);
  void MovePointer(uint64_t time, double x, double y);
  void ReportModifiers(uint64_t time);
  void StartRepeat(uint32_t keycode);
  void StopRepeat();
  void RepeatKey(uint64_t generation, EventLoop::TimePoint target_time);

  EventLoop* event_loop_;
  std::unique_ptr<XkbKeyboard> keyboard_;
  KeyResolver key_resolver_;
  InputHandler* input_handler_ = nullptr;
  KeyboardHandler* keyboard_handler_ = nullptr;
  bool active_ = true;
  bool has_focus_ = false;

  // The XKB keycodes of the pressed keys.
  std::set<uint32_t> pressed_keys_;
  // Keys whose press was consumed, so their release isn't sent.
  std::set<uint32_t> consumed_keys_;
  std::optional<KeyModifiers> reported_modifiers_;
  std::optional<KeyModifiers> led_modifiers_;

  uint32_t repeat_keycode_ = 0;
  uint64_t repeat_generation_ = 0;

  int32_t screen_width_ = 0;
  int32_t screen_height_ = 0;
  double scale_ = 1.0;
  double pointer_x_ = 0;
  double pointer_y_ = 0;
  bool has_pointer_ = false;
  bool pointer_entered_ = false;
  uint32_t button_serial_ = 0;
  std::set<int32_t> touches_;

  std::function<void(uint32_t)> switch_vt_callback_;
  std::function<void()> pointer_callback_;
  std::function<void(const KeyModifiers&)> leds_callback_;

  // Reset on destruction, so tasks can detect it.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(KmsSeat);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_SEAT_H_
