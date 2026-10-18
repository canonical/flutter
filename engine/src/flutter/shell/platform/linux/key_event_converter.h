// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_KEY_EVENT_CONVERTER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_KEY_EVENT_CONVERTER_H_

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/xkb.h"

namespace flutter {

// A key, as interpreted by the keymap in use when it was pressed.
struct KeyboardKey {
  // The XKB keycode, which is the Linux input event code plus 8.
  uint32_t keycode = 0;

  // The keysym the key produces.
  uint32_t keysym = 0;

  // The Flutter logical key.
  uint64_t logical_key = 0;

  // True if |logical_key| was chosen to match the key on the US layout,
  // rather than from what the key produces.
  bool layout_logical_key = false;

  // The printable character the key produces, ignoring modifiers such as
  // Control that don't change the keysym, or 0 if none.
  uint32_t character = 0;

  // The character the key types, after transformations such as Control, or 0
  // if none.
  uint32_t text = 0;
};

enum class KeyAction {
  kDown,
  kUp,
  kRepeat,
};

// Returns the Flutter physical key for an XKB keycode.
uint64_t GetPhysicalKey(uint32_t keycode);

// Returns the Flutter logical key for a key that produces |keysym|.
// |lower_character| is the character the lowercase form of the keysym
// produces, or 0 if none.
uint64_t GetLogicalKey(uint32_t keysym, uint32_t lower_character);

// Returns true if |character| is printable, i.e. not a control character.
bool IsPrintable(uint32_t character);

// Returns a Unicode |character| in UTF-8.
std::string CharacterToUtf8(uint32_t character);

// The value DeriveLayoutLogicalKeys's |get_character| returns for keys that
// produce a keysym that is not a character.
constexpr uint32_t kLayoutNotCharacter = 0xffff;

// Chooses logical keys for keys in a keyboard layout so that every layout has
// keys for the letters and digits, which are used in shortcuts. For example,
// on a Cyrillic layout the key that types "c" on the US layout is given the
// logical key "c", so Ctrl+C works.
//
// |get_character| returns the character a key produces at a shift level, 0
// if the key produces nothing, or 0xffff if it produces a keysym that is not
// a character.
//
// Returns the logical key for each XKB keycode that should use one.
std::unordered_map<uint32_t, uint64_t> DeriveLayoutLogicalKeys(
    const std::function<uint32_t(uint32_t keycode, uint32_t level)>&
        get_character);

// Converts key events from the keyboard into Flutter key events, keeping
// track of the keys that Flutter has been told are pressed.
class KeyEventConverter {
 public:
  // Sends |event| to Flutter. |callback| is called with whether Flutter
  // handled it, and may be empty if the result is not needed.
  using SendCallback =
      std::function<void(const FlutterKeyEvent& event,
                         std::function<void(bool handled)> callback)>;

  explicit KeyEventConverter(SendCallback send);

  // Converts a key event, sending any events needed to synchronize the
  // pressed keys first. |callback| is called with whether Flutter handled
  // the key.
  //
  // At least one event is always sent, so each key event can be matched
  // with the message sent on the flutter/keyevent channel.
  void HandleKey(uint64_t time,
                 const KeyboardKey& key,
                 KeyAction action,
                 std::function<void(bool handled)> callback);

  // Sends synthesized events so Flutter considers exactly |keys| pressed.
  void SyncPressedKeys(uint64_t time, const std::vector<KeyboardKey>& keys);

  // Sends synthesized events so Flutter's lock states match |modifiers|.
  void SyncLockStates(uint64_t time, const KeyModifiers& modifiers);

  // The logical key for each pressed physical key.
  const std::map<uint64_t, uint64_t>& pressed_keys() const {
    return pressed_keys_;
  }

 private:
  // Sends an event that didn't come from the keyboard.
  void SendSynthesized(uint64_t time,
                       FlutterKeyEventType type,
                       uint64_t physical,
                       uint64_t logical);

  // Toggles the recorded lock state if |logical| is a lock key, as Flutter
  // does when it is pressed.
  void UpdateLockState(uint64_t logical);

  // Returns true if a key with logical key |logical| is pressed.
  bool IsLogicalKeyPressed(uint64_t logical) const;

  SendCallback send_;
  std::map<uint64_t, uint64_t> pressed_keys_;

  // The lock states Flutter has been told about, indexed by lock key.
  std::map<uint64_t, bool> lock_states_;

  FML_DISALLOW_COPY_AND_ASSIGN(KeyEventConverter);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_KEY_EVENT_CONVERTER_H_
