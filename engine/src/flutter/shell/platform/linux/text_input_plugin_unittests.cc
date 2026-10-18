// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/text_input_plugin.h"

#include "flutter/shell/platform/linux/fake_platform_messenger.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

constexpr char kChannel[] = "flutter/textinput";

constexpr uint32_t kControlMask = 1 << 2;

KeyboardKey CharacterKey(uint32_t character) {
  return {.keysym = character, .character = character, .text = character};
}

KeyboardKey ReturnKey() {
  return {.keycode = 36, .keysym = 0xff0d, .text = '\r'};
}

// Returns the method name and arguments of a message sent on the channel.
std::unique_ptr<MethodCall<rapidjson::Document>> DecodeCall(
    const FakePlatformMessenger& messenger,
    size_t index) {
  EXPECT_EQ(messenger.sent[index].channel, kChannel);
  return JsonMethodCodec::GetInstance().DecodeMethodCall(
      messenger.sent[index].message);
}

std::string SuccessEnvelope() {
  auto envelope = JsonMethodCodec::GetInstance().EncodeSuccessEnvelope();
  return std::string(envelope->begin(), envelope->end());
}

}  // namespace

TEST(TextInputPluginTest, NoClient) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  EXPECT_FALSE(plugin.HandleKey(CharacterKey('a'), {}));
  EXPECT_TRUE(messenger.sent.empty());
}

TEST(TextInputPluginTest, TypeText) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  std::vector<uint8_t> response = messenger.ReceiveJsonMethodCall(
      kChannel, "TextInput.setClient",
      R"([1, {"inputAction": "TextInputAction.done",
              "inputType": {"name": "TextInputType.text"}}])");
  EXPECT_EQ(std::string(response.begin(), response.end()), SuccessEnvelope());

  EXPECT_TRUE(plugin.HandleKey(CharacterKey('a'), {}));
  EXPECT_TRUE(plugin.HandleKey(CharacterKey(0xe9), {}));

  ASSERT_EQ(messenger.sent.size(), 2u);
  auto call = DecodeCall(messenger, 1);
  EXPECT_EQ(call->method_name(), "TextInputClient.updateEditingState");
  const rapidjson::Document& args = *call->arguments();
  EXPECT_EQ(args[0].GetInt(), 1);
  EXPECT_STREQ(args[1]["text"].GetString(), "a\xc3\xa9");
  EXPECT_EQ(args[1]["selectionBase"].GetInt(), 2);
  EXPECT_EQ(args[1]["selectionExtent"].GetInt(), 2);
  EXPECT_EQ(args[1]["composingBase"].GetInt(), -1);
  EXPECT_EQ(args[1]["composingExtent"].GetInt(), -1);
}

TEST(TextInputPluginTest, SetEditingState) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  messenger.ReceiveJsonMethodCall(kChannel, "TextInput.setClient",
                                  R"([1, {}])");
  messenger.ReceiveJsonMethodCall(
      kChannel, "TextInput.setEditingState",
      R"({"text": "hello", "selectionBase": 1, "selectionExtent": 1})");

  EXPECT_TRUE(plugin.HandleKey(CharacterKey('x'), {}));
  ASSERT_EQ(messenger.sent.size(), 1u);
  auto call = DecodeCall(messenger, 0);
  EXPECT_STREQ((*call->arguments())[1]["text"].GetString(), "hxello");
}

TEST(TextInputPluginTest, ControlDoesNotType) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  messenger.ReceiveJsonMethodCall(kChannel, "TextInput.setClient",
                                  R"([1, {}])");

  KeyModifiers modifiers;
  modifiers.mask = kControlMask;
  EXPECT_FALSE(plugin.HandleKey(CharacterKey('a'), modifiers));
  EXPECT_TRUE(messenger.sent.empty());
}

TEST(TextInputPluginTest, EnterPerformsAction) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  messenger.ReceiveJsonMethodCall(kChannel, "TextInput.setClient",
                                  R"([3, {"inputAction": "TextInputAction.done",
              "inputType": {"name": "TextInputType.text"}}])");

  EXPECT_TRUE(plugin.HandleKey(ReturnKey(), {}));
  ASSERT_EQ(messenger.sent.size(), 1u);
  auto call = DecodeCall(messenger, 0);
  EXPECT_EQ(call->method_name(), "TextInputClient.performAction");
  EXPECT_EQ((*call->arguments())[0].GetInt(), 3);
  EXPECT_STREQ((*call->arguments())[1].GetString(), "TextInputAction.done");
}

TEST(TextInputPluginTest, EnterInsertsNewline) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  messenger.ReceiveJsonMethodCall(
      kChannel, "TextInput.setClient",
      R"([1, {"inputAction": "TextInputAction.newline",
              "inputType": {"name": "TextInputType.multiline"}}])");

  EXPECT_TRUE(plugin.HandleKey(ReturnKey(), {}));
  ASSERT_EQ(messenger.sent.size(), 2u);
  auto update = DecodeCall(messenger, 0);
  EXPECT_EQ(update->method_name(), "TextInputClient.updateEditingState");
  EXPECT_STREQ((*update->arguments())[1]["text"].GetString(), "\n");
  auto action = DecodeCall(messenger, 1);
  EXPECT_EQ(action->method_name(), "TextInputClient.performAction");
}

TEST(TextInputPluginTest, Deltas) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  messenger.ReceiveJsonMethodCall(kChannel, "TextInput.setClient",
                                  R"([1, {"enableDeltaModel": true}])");

  EXPECT_TRUE(plugin.HandleKey(CharacterKey('a'), {}));
  ASSERT_EQ(messenger.sent.size(), 1u);
  auto call = DecodeCall(messenger, 0);
  EXPECT_EQ(call->method_name(),
            "TextInputClient.updateEditingStateWithDeltas");
  const rapidjson::Value& delta = (*call->arguments())[1]["deltas"][0];
  EXPECT_STREQ(delta["oldText"].GetString(), "");
  EXPECT_STREQ(delta["deltaText"].GetString(), "a");
  EXPECT_EQ(delta["deltaStart"].GetInt(), 0);
  EXPECT_EQ(delta["deltaEnd"].GetInt(), 0);
}

TEST(TextInputPluginTest, ClearClient) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  messenger.ReceiveJsonMethodCall(kChannel, "TextInput.setClient",
                                  R"([1, {}])");
  messenger.ReceiveJsonMethodCall(kChannel, "TextInput.clearClient", "null");
  EXPECT_FALSE(plugin.HandleKey(CharacterKey('a'), {}));
}

TEST(TextInputPluginTest, UnknownMethod) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  EXPECT_TRUE(
      messenger.ReceiveJsonMethodCall(kChannel, "TextInput.unknown", "null")
          .empty());
}

}  // namespace testing
}  // namespace flutter
