// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_KEYBOARD_MANAGER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_KEYBOARD_MANAGER_H_

#include <memory>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/key_event_converter.h"
#include "flutter/shell/platform/linux/platform_messenger.h"
#include "flutter/shell/platform/linux/text_input_plugin.h"
#include "flutter/shell/platform/linux/wayland_seat.h"

namespace flutter {

// Sends keyboard input for an engine to Flutter.
//
// Each key event is sent both as a FlutterKeyEvent and on the
// flutter/keyevent channel, as the framework requires. Keys neither handles
// are passed to the text input plugin.
class KeyboardManager : public KeyboardHandler {
 public:
  // |messenger| must outlive the manager.
  KeyboardManager(PlatformMessenger* messenger,
                  KeyEventConverter::SendCallback send_key_event);
  ~KeyboardManager() override;

  // |KeyboardHandler|
  void OnKeyboardEnter(uint64_t time,
                       const std::vector<KeyboardKey>& keys,
                       const KeyModifiers& modifiers) override;

  // |KeyboardHandler|
  void OnKeyboardLeave(uint64_t time) override;

  // |KeyboardHandler|
  void OnKey(uint64_t time,
             const KeyboardKey& key,
             KeyAction action,
             const KeyModifiers& modifiers) override;

  // |KeyboardHandler|
  void OnKeyModifiers(uint64_t time, const KeyModifiers& modifiers) override;

  // Edits text fields, with keys and the input method.
  TextInputPlugin* text_input() { return &text_input_; }

 private:
  // Sends |key| on the flutter/keyevent channel. |callback| is called with
  // whether the framework handled it.
  void SendChannelEvent(const KeyboardKey& key,
                        KeyAction action,
                        const KeyModifiers& modifiers,
                        std::function<void(bool handled)> callback);

  // Handles a method call on the flutter/keyboard channel, returning the
  // encoded response.
  std::vector<uint8_t> HandleKeyboardMethodCall(const uint8_t* data,
                                                size_t size);

  PlatformMessenger* messenger_;
  KeyEventConverter converter_;
  TextInputPlugin text_input_;

  // Callbacks hold a weak reference to this, so they don't run after the
  // manager is destroyed.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(KeyboardManager);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_KEYBOARD_MANAGER_H_
