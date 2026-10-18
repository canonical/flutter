// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/keyboard_manager.h"

#include "flutter/shell/platform/linux/fake_platform_messenger.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

constexpr uint32_t kKeycodeA = 38;
constexpr uint32_t kKeycodeShiftLeft = 50;
constexpr uint64_t kPhysicalKeyA = 0x00070004;
constexpr uint64_t kLogicalKeyA = 0x00000000061;
constexpr uint64_t kPhysicalShiftLeft = 0x000700e1;
constexpr uint64_t kLogicalShiftLeft = 0x00200000102;

KeyboardKey KeyA() {
  return {.keycode = kKeycodeA,
          .keysym = 0x61,
          .logical_key = kLogicalKeyA,
          .character = 'a',
          .text = 'a'};
}

std::vector<uint8_t> HandledReply(bool handled) {
  std::string reply =
      std::string("{\"handled\":") + (handled ? "true" : "false") + "}";
  return std::vector<uint8_t>(reply.begin(), reply.end());
}

class KeyboardManagerTest : public ::testing::Test {
 protected:
  KeyboardManagerTest()
      : manager_(&messenger_,
                 [this](const FlutterKeyEvent& event,
                        std::function<void(bool)> callback) {
                   events_.push_back(event);
                   callbacks_.push_back(std::move(callback));
                 }) {
    messenger_.ReceiveJsonMethodCall("flutter/textinput", "TextInput.setClient",
                                     R"([1, {}])");
  }

  // Returns the number of text input updates sent.
  size_t TextInputUpdates() const {
    size_t count = 0;
    for (const auto& message : messenger_.sent) {
      count += message.channel == "flutter/textinput" ? 1 : 0;
    }
    return count;
  }

  FakePlatformMessenger messenger_;
  std::vector<FlutterKeyEvent> events_;
  std::vector<std::function<void(bool)>> callbacks_;
  KeyboardManager manager_;
};

}  // namespace

TEST_F(KeyboardManagerTest, ChannelMessage) {
  KeyModifiers modifiers;
  modifiers.mask = 1 << 6;
  manager_.OnKey(1000, KeyA(), KeyAction::kDown, modifiers);

  ASSERT_EQ(events_.size(), 1u);
  ASSERT_EQ(messenger_.sent.size(), 1u);
  EXPECT_EQ(messenger_.sent[0].channel, "flutter/keyevent");
  auto message = messenger_.DecodeJson(0);
  EXPECT_STREQ((*message)["type"].GetString(), "keydown");
  EXPECT_STREQ((*message)["keymap"].GetString(), "linux");
  EXPECT_STREQ((*message)["toolkit"].GetString(), "gtk");
  EXPECT_EQ((*message)["scanCode"].GetInt(), 38);
  EXPECT_EQ((*message)["keyCode"].GetInt(), 0x61);
  EXPECT_EQ((*message)["modifiers"].GetInt(), (1 << 6) | (1 << 26));
  EXPECT_EQ((*message)["unicodeScalarValues"].GetInt(), 'a');
  EXPECT_FALSE(message->HasMember("specifiedLogicalKey"));

  KeyboardKey layout_key = KeyA();
  layout_key.layout_logical_key = true;
  manager_.OnKey(2000, layout_key, KeyAction::kUp, {});
  auto up_message = messenger_.DecodeJson(1);
  EXPECT_STREQ((*up_message)["type"].GetString(), "keyup");
  EXPECT_EQ((*up_message)["specifiedLogicalKey"].GetUint64(), kLogicalKeyA);
}

TEST_F(KeyboardManagerTest, UnhandledKeyTypes) {
  manager_.OnKey(1000, KeyA(), KeyAction::kDown, {});
  ASSERT_EQ(callbacks_.size(), 1u);
  ASSERT_EQ(messenger_.sent.size(), 1u);

  // Text is only typed once both responses arrive.
  callbacks_[0](false);
  EXPECT_EQ(TextInputUpdates(), 0u);
  auto reply = HandledReply(false);
  messenger_.sent[0].reply(reply.data(), reply.size());
  EXPECT_EQ(TextInputUpdates(), 1u);
}

TEST_F(KeyboardManagerTest, HandledKeyDoesNotType) {
  manager_.OnKey(1000, KeyA(), KeyAction::kDown, {});
  auto reply = HandledReply(true);
  messenger_.sent[0].reply(reply.data(), reply.size());
  callbacks_[0](false);
  EXPECT_EQ(TextInputUpdates(), 0u);

  manager_.OnKey(2000, KeyA(), KeyAction::kRepeat, {});
  callbacks_[1](true);
  messenger_.sent[1].reply(nullptr, 0);
  EXPECT_EQ(TextInputUpdates(), 0u);
}

TEST_F(KeyboardManagerTest, ReleaseDoesNotType) {
  manager_.OnKey(1000, KeyA(), KeyAction::kDown, {});
  manager_.OnKey(2000, KeyA(), KeyAction::kUp, {});
  callbacks_[1](false);
  messenger_.sent[1].reply(nullptr, 0);
  EXPECT_EQ(TextInputUpdates(), 0u);
}

TEST_F(KeyboardManagerTest, GetKeyboardState) {
  manager_.OnKeyboardEnter(1000,
                           {{.keycode = kKeycodeShiftLeft,
                             .keysym = 0xffe1,
                             .logical_key = kLogicalShiftLeft}},
                           {});

  // A standard method call for getKeyboardState with no arguments.
  std::string method = "getKeyboardState";
  std::vector<uint8_t> call = {7, static_cast<uint8_t>(method.size())};
  call.insert(call.end(), method.begin(), method.end());
  call.push_back(0);
  std::vector<uint8_t> response = messenger_.Receive("flutter/keyboard", call);

  // Success, a map with one entry, then two int64s aligned to 8 bytes.
  std::vector<uint8_t> expected = {0, 13, 1, 4, 0, 0, 0, 0};
  for (uint64_t value : {kPhysicalShiftLeft, kLogicalShiftLeft}) {
    for (int i = 0; i < 8; i++) {
      expected.push_back((value >> (i * 8)) & 0xff);
    }
    if (value == kPhysicalShiftLeft) {
      expected.insert(expected.end(), {4, 0, 0, 0, 0, 0, 0, 0});
    }
  }
  EXPECT_EQ(response, expected);
}

TEST_F(KeyboardManagerTest, LeaveReleasesKeys) {
  manager_.OnKey(1000, KeyA(), KeyAction::kDown, {});
  manager_.OnKeyboardLeave(2000);
  ASSERT_EQ(events_.size(), 2u);
  EXPECT_EQ(events_[1].type, kFlutterKeyEventTypeUp);
  EXPECT_EQ(events_[1].physical, kPhysicalKeyA);
  EXPECT_TRUE(events_[1].synthesized);
}

TEST_F(KeyboardManagerTest, UnknownKeyboardMethod) {
  std::vector<uint8_t> call = {7, 3, 'f', 'o', 'o', 0};
  EXPECT_TRUE(messenger_.Receive("flutter/keyboard", call).empty());
}

}  // namespace testing
}  // namespace flutter
