// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/keyboard_manager.h"

#include <cstring>
#include <string>

#include "flutter/shell/platform/common/json_message_codec.h"

namespace flutter {

namespace {

constexpr char kKeyEventChannel[] = "flutter/keyevent";
constexpr char kKeyboardChannel[] = "flutter/keyboard";

constexpr char kGetKeyboardStateMethod[] = "getKeyboardState";

// The flutter/keyevent message uses the format of the GTK embedder, which
// the framework decodes as RawKeyEventDataLinux.
constexpr char kTypeKey[] = "type";
constexpr char kTypeDown[] = "keydown";
constexpr char kTypeUp[] = "keyup";
constexpr char kKeymapKey[] = "keymap";
constexpr char kKeymapLinux[] = "linux";
constexpr char kToolkitKey[] = "toolkit";
constexpr char kToolkitGtk[] = "gtk";
constexpr char kScanCodeKey[] = "scanCode";
constexpr char kKeyCodeKey[] = "keyCode";
constexpr char kModifiersKey[] = "modifiers";
constexpr char kUnicodeScalarValuesKey[] = "unicodeScalarValues";
constexpr char kSpecifiedLogicalKeyKey[] = "specifiedLogicalKey";
constexpr char kHandledKey[] = "handled";

// The GDK modifier for the Super key, which XKB usually maps to Mod4, and
// which the framework treats as Meta.
constexpr uint32_t kMod4Mask = 1 << 6;
constexpr uint32_t kGdkSuperMask = 1 << 26;

// Values in the standard message codec.
constexpr uint8_t kStandardSuccessEnvelope = 0;
constexpr uint8_t kStandardInt64 = 4;
constexpr uint8_t kStandardString = 7;
constexpr uint8_t kStandardMap = 13;

// Writes a size in the standard message codec.
void WriteStandardSize(std::vector<uint8_t>& buffer, uint32_t size) {
  if (size < 254) {
    buffer.push_back(size);
  } else if (size <= 0xffff) {
    buffer.push_back(254);
    buffer.push_back(size & 0xff);
    buffer.push_back(size >> 8);
  } else {
    buffer.push_back(255);
    for (int i = 0; i < 4; i++) {
      buffer.push_back((size >> (i * 8)) & 0xff);
    }
  }
}

// Writes an integer in the standard message codec.
void WriteStandardInt64(std::vector<uint8_t>& buffer, int64_t value) {
  buffer.push_back(kStandardInt64);
  while (buffer.size() % 8 != 0) {
    buffer.push_back(0);
  }
  uint64_t bits = static_cast<uint64_t>(value);
  for (int i = 0; i < 8; i++) {
    buffer.push_back((bits >> (i * 8)) & 0xff);
  }
}

// Reads the method name of a method call in the standard method codec.
// Returns an empty string if the message is invalid.
std::string ReadStandardMethodName(const uint8_t* data, size_t size) {
  if (size < 2 || data[0] != kStandardString) {
    return "";
  }
  size_t offset = 1;
  uint32_t length = data[offset++];
  if (length == 254) {
    if (offset + 2 > size) {
      return "";
    }
    length = data[offset] | (data[offset + 1] << 8);
    offset += 2;
  } else if (length == 255) {
    if (offset + 4 > size) {
      return "";
    }
    memcpy(&length, data + offset, 4);
    offset += 4;
  }
  if (offset + length > size) {
    return "";
  }
  return std::string(reinterpret_cast<const char*>(data + offset), length);
}

// Combines the results of sending a key event both ways.
struct PendingKeyEvent {
  KeyboardKey key;
  KeyModifiers modifiers;
  int remaining = 2;
  bool handled = false;
};

}  // namespace

KeyboardManager::KeyboardManager(PlatformMessenger* messenger,
                                 KeyEventConverter::SendCallback send_key_event)
    : messenger_(messenger),
      converter_(std::move(send_key_event)),
      text_input_(messenger) {
  messenger_->SetMessageHandler(
      kKeyboardChannel,
      [this](const uint8_t* data, size_t size,
             const PlatformMessenger::ResponseCallback& respond) {
        respond(HandleKeyboardMethodCall(data, size));
      });
}

KeyboardManager::~KeyboardManager() {
  messenger_->SetMessageHandler(kKeyboardChannel, nullptr);
}

void KeyboardManager::OnKeyboardEnter(uint64_t time,
                                      const std::vector<KeyboardKey>& keys,
                                      const KeyModifiers& modifiers) {
  converter_.SyncPressedKeys(time, keys);
  converter_.SyncLockStates(time, modifiers);
}

void KeyboardManager::OnKeyboardLeave(uint64_t time) {
  converter_.SyncPressedKeys(time, {});
}

void KeyboardManager::OnKey(uint64_t time,
                            const KeyboardKey& key,
                            KeyAction action,
                            const KeyModifiers& modifiers) {
  auto pending = std::make_shared<PendingKeyEvent>();
  pending->key = key;
  pending->modifiers = modifiers;
  auto on_result = [this, weak_alive = std::weak_ptr<bool>(alive_), pending,
                    action](bool handled) {
    if (weak_alive.expired()) {
      return;
    }
    pending->handled = pending->handled || handled;
    if (--pending->remaining > 0 || pending->handled ||
        action == KeyAction::kUp) {
      return;
    }
    text_input_.HandleKey(pending->key, pending->modifiers);
  };

  converter_.HandleKey(time, key, action, on_result);
  SendChannelEvent(key, action, modifiers, on_result);
}

void KeyboardManager::OnKeyModifiers(uint64_t time,
                                     const KeyModifiers& modifiers) {
  converter_.SyncLockStates(time, modifiers);
}

void KeyboardManager::SendChannelEvent(
    const KeyboardKey& key,
    KeyAction action,
    const KeyModifiers& modifiers,
    std::function<void(bool handled)> callback) {
  uint32_t state = modifiers.mask;
  if ((state & kMod4Mask) != 0) {
    state |= kGdkSuperMask;
  }

  rapidjson::Document message(rapidjson::kObjectType);
  auto& allocator = message.GetAllocator();
  message.AddMember(
      kTypeKey,
      rapidjson::StringRef(action == KeyAction::kUp ? kTypeUp : kTypeDown),
      allocator);
  message.AddMember(kKeymapKey, kKeymapLinux, allocator);
  message.AddMember(kToolkitKey, kToolkitGtk, allocator);
  message.AddMember(kScanCodeKey, key.keycode, allocator);
  message.AddMember(kKeyCodeKey, key.keysym, allocator);
  message.AddMember(kModifiersKey, state, allocator);
  if (IsPrintable(key.character)) {
    message.AddMember(kUnicodeScalarValuesKey, key.character, allocator);
  }
  if (key.layout_logical_key) {
    message.AddMember(kSpecifiedLogicalKeyKey, key.logical_key, allocator);
  }

  const JsonMessageCodec& codec = JsonMessageCodec::GetInstance();
  messenger_->SendMessage(
      kKeyEventChannel, *codec.EncodeMessage(message),
      [callback = std::move(callback)](const uint8_t* data, size_t size) {
        bool handled = false;
        if (size > 0) {
          std::unique_ptr<rapidjson::Document> reply =
              JsonMessageCodec::GetInstance().DecodeMessage(data, size);
          if (reply != nullptr && reply->IsObject()) {
            auto it = reply->FindMember(kHandledKey);
            handled = it != reply->MemberEnd() && it->value.IsBool() &&
                      it->value.GetBool();
          }
        }
        callback(handled);
      });
}

std::vector<uint8_t> KeyboardManager::HandleKeyboardMethodCall(
    const uint8_t* data,
    size_t size) {
  if (ReadStandardMethodName(data, size) != kGetKeyboardStateMethod) {
    return {};
  }

  // Returns a map of the pressed physical keys to their logical keys.
  const std::map<uint64_t, uint64_t>& pressed_keys = converter_.pressed_keys();
  std::vector<uint8_t> response;
  response.push_back(kStandardSuccessEnvelope);
  response.push_back(kStandardMap);
  WriteStandardSize(response, pressed_keys.size());
  for (const auto& [physical, logical] : pressed_keys) {
    WriteStandardInt64(response, physical);
    WriteStandardInt64(response, logical);
  }
  return response;
}

}  // namespace flutter
