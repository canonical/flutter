// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/key_event_converter.h"

#include <set>
#include <string>

#include "flutter/shell/platform/linux/key_mapping.h"

namespace flutter {

namespace {

// Keys that toggle a lock state.
struct LockKey {
  uint64_t physical;
  uint64_t logical;
  bool KeyModifiers::* state;
};

constexpr LockKey kLockKeys[] = {
    {0x00070039, 0x00100000104, &KeyModifiers::caps_lock},
    {0x00070053, 0x0010000010a, &KeyModifiers::num_lock},
    {0x00070047, 0x0010000010c, &KeyModifiers::scroll_lock},
};

}  // namespace

std::string CharacterToUtf8(uint32_t character) {
  std::string result;
  if (character < 0x80) {
    result += static_cast<char>(character);
  } else if (character < 0x800) {
    result += static_cast<char>(0xc0 | (character >> 6));
    result += static_cast<char>(0x80 | (character & 0x3f));
  } else if (character < 0x10000) {
    result += static_cast<char>(0xe0 | (character >> 12));
    result += static_cast<char>(0x80 | ((character >> 6) & 0x3f));
    result += static_cast<char>(0x80 | (character & 0x3f));
  } else {
    result += static_cast<char>(0xf0 | (character >> 18));
    result += static_cast<char>(0x80 | ((character >> 12) & 0x3f));
    result += static_cast<char>(0x80 | ((character >> 6) & 0x3f));
    result += static_cast<char>(0x80 | (character & 0x3f));
  }
  return result;
}

uint64_t GetPhysicalKey(uint32_t keycode) {
  auto it = kXkbKeycodeToPhysicalKey.find(keycode);
  if (it != kXkbKeycodeToPhysicalKey.end()) {
    return it->second;
  }
  return (keycode & kValueMask) | kLinuxPlane;
}

uint64_t GetLogicalKey(uint32_t keysym, uint32_t lower_character) {
  auto it = kKeysymToLogicalKey.find(keysym);
  if (it != kKeysymToLogicalKey.end()) {
    return it->second;
  }
  if (IsPrintable(lower_character)) {
    return lower_character | kUnicodePlane;
  }
  return (keysym & kValueMask) | kLinuxPlane;
}

bool IsPrintable(uint32_t character) {
  return character >= 0x20 && !(character >= 0x7f && character < 0xa0) &&
         character <= 0x10ffff;
}

std::unordered_map<uint32_t, uint64_t> DeriveLayoutLogicalKeys(
    const std::function<uint32_t(uint32_t keycode, uint32_t level)>&
        get_character) {
  std::unordered_map<uint32_t, uint64_t> result;

  std::set<uint64_t> remaining_mandatory_keys;
  for (const LayoutGoal& goal : kLayoutGoals) {
    if (goal.mandatory) {
      remaining_mandatory_keys.insert(goal.logical_key);
    }
  }

  for (const LayoutGoal& goal : kLayoutGoals) {
    uint32_t characters[] = {get_character(goal.keycode, 0),
                             get_character(goal.keycode, 1)};

    // Use the first mandatory key this key produces, unshifted or shifted,
    // e.g. the digits on the French layout which are typed with Shift.
    for (uint32_t character : characters) {
      if (remaining_mandatory_keys.erase(character) > 0) {
        result[goal.keycode] = character;
        break;
      }
    }

    // Keys that produce no Latin-1 character, e.g. on Cyrillic layouts, use
    // the US layout.
    bool has_latin1 = characters[0] < 0x100 || characters[1] < 0x100;
    if (result.count(goal.keycode) == 0 && !has_latin1) {
      result[goal.keycode] = goal.logical_key;
    }
  }

  // Mandatory keys not found on the layout use the key from the US layout.
  for (const LayoutGoal& goal : kLayoutGoals) {
    if (goal.mandatory && remaining_mandatory_keys.count(goal.logical_key)) {
      result[goal.keycode] = goal.logical_key;
    }
  }

  return result;
}

KeyEventConverter::KeyEventConverter(SendCallback send)
    : send_(std::move(send)) {}

void KeyEventConverter::HandleKey(uint64_t time,
                                  const KeyboardKey& key,
                                  KeyAction action,
                                  std::function<void(bool handled)> callback) {
  uint64_t physical = GetPhysicalKey(key.keycode);
  auto pressed = pressed_keys_.find(physical);

  FlutterKeyEvent event = {};
  event.struct_size = sizeof(FlutterKeyEvent);
  event.timestamp = static_cast<double>(time);
  event.physical = physical;
  event.logical = key.logical_key;
  event.synthesized = false;
  event.device_type = kFlutterKeyEventDeviceTypeKeyboard;

  std::string character;
  if (action == KeyAction::kUp) {
    if (pressed == pressed_keys_.end()) {
      // Flutter doesn't think this key is pressed, e.g. it was pressed
      // before the window had focus.
      FlutterKeyEvent empty = {};
      empty.struct_size = sizeof(FlutterKeyEvent);
      empty.timestamp = static_cast<double>(time);
      empty.type = kFlutterKeyEventTypeDown;
      send_(empty, nullptr);
      callback(true);
      return;
    }
    event.type = kFlutterKeyEventTypeUp;
    event.logical = pressed->second;
    pressed_keys_.erase(pressed);
  } else {
    if (pressed != pressed_keys_.end()) {
      // Repeated keys keep the logical key they were pressed with, even if
      // the modifiers have changed.
      event.type = kFlutterKeyEventTypeRepeat;
      event.logical = pressed->second;
    } else {
      event.type = kFlutterKeyEventTypeDown;
      pressed_keys_[physical] = event.logical;
      UpdateLockState(event.logical);
    }
    if (IsPrintable(key.character)) {
      character = CharacterToUtf8(key.character);
      event.character = character.c_str();
    }
  }

  send_(event, std::move(callback));
}

void KeyEventConverter::SyncPressedKeys(uint64_t time,
                                        const std::vector<KeyboardKey>& keys) {
  std::map<uint64_t, uint64_t> target;
  for (const KeyboardKey& key : keys) {
    target.emplace(GetPhysicalKey(key.keycode), key.logical_key);
  }

  for (auto it = pressed_keys_.begin(); it != pressed_keys_.end();) {
    if (target.count(it->first) > 0) {
      ++it;
      continue;
    }
    uint64_t physical = it->first;
    uint64_t logical = it->second;
    it = pressed_keys_.erase(it);
    SendSynthesized(time, kFlutterKeyEventTypeUp, physical, logical);
  }

  for (const auto& [physical, logical] : target) {
    if (pressed_keys_.count(physical) > 0) {
      continue;
    }
    pressed_keys_[physical] = logical;
    UpdateLockState(logical);
    SendSynthesized(time, kFlutterKeyEventTypeDown, physical, logical);
  }
}

void KeyEventConverter::SyncLockStates(uint64_t time,
                                       const KeyModifiers& modifiers) {
  for (const LockKey& lock : kLockKeys) {
    bool enabled = modifiers.*(lock.state);
    if (lock_states_[lock.logical] == enabled) {
      continue;
    }

    // While a lock key is held the compositor's state may not have changed
    // yet, e.g. XKB unlocks Caps Lock when it is released.
    if (IsLogicalKeyPressed(lock.logical)) {
      continue;
    }

    // Flutter toggles the lock state each time the key is pressed.
    lock_states_[lock.logical] = enabled;
    SendSynthesized(time, kFlutterKeyEventTypeDown, lock.physical,
                    lock.logical);
    SendSynthesized(time, kFlutterKeyEventTypeUp, lock.physical, lock.logical);
  }
}

void KeyEventConverter::SendSynthesized(uint64_t time,
                                        FlutterKeyEventType type,
                                        uint64_t physical,
                                        uint64_t logical) {
  FlutterKeyEvent event = {};
  event.struct_size = sizeof(FlutterKeyEvent);
  event.timestamp = static_cast<double>(time);
  event.type = type;
  event.physical = physical;
  event.logical = logical;
  event.synthesized = true;
  event.device_type = kFlutterKeyEventDeviceTypeKeyboard;
  send_(event, nullptr);
}

void KeyEventConverter::UpdateLockState(uint64_t logical) {
  for (const LockKey& lock : kLockKeys) {
    if (lock.logical == logical) {
      lock_states_[logical] = !lock_states_[logical];
    }
  }
}

bool KeyEventConverter::IsLogicalKeyPressed(uint64_t logical) const {
  for (const auto& [physical, pressed_logical] : pressed_keys_) {
    if (pressed_logical == logical) {
      return true;
    }
  }
  return false;
}

namespace {

// Returns the character produced by |keysym| for deriving layouts.
uint32_t LayoutCharacter(uint32_t keysym) {
  if (keysym == 0) {
    return 0;
  }
  uint32_t character = KeysymToUtf32(keysym);
  return character != 0 ? character : kLayoutNotCharacter;
}

}  // namespace

KeyboardKey KeyResolver::Resolve(const XkbKeyboard& keyboard,
                                 uint32_t keycode) {
  KeyboardKey key;
  key.keycode = keycode;
  key.keysym = keyboard.GetKeysym(keycode);
  key.character = KeysymToUtf32(key.keysym);
  key.text = keyboard.GetText(keycode);

  const std::unordered_map<uint32_t, uint64_t>& layout_keys =
      LayoutLogicalKeys(keyboard, keyboard.GetLayout(keycode));
  auto it = layout_keys.find(keycode);
  if (it != layout_keys.end()) {
    key.logical_key = it->second;
    key.layout_logical_key = true;
  } else {
    key.logical_key =
        GetLogicalKey(key.keysym, KeysymToUtf32(KeysymToLower(key.keysym)));
  }
  return key;
}

const std::unordered_map<uint32_t, uint64_t>& KeyResolver::LayoutLogicalKeys(
    const XkbKeyboard& keyboard,
    uint32_t layout) {
  auto it = layout_keys_.find(layout);
  if (it != layout_keys_.end()) {
    return it->second;
  }
  return layout_keys_[layout] = DeriveLayoutLogicalKeys(
             [&keyboard, layout](uint32_t keycode, uint32_t level) {
               return LayoutCharacter(
                   keyboard.GetLevelKeysym(keycode, layout, level));
             });
}

}  // namespace flutter
