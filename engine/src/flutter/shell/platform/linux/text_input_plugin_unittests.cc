// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/text_input_plugin.h"

#include "flutter/shell/platform/linux/fake_platform_messenger.h"
#include "flutter/shell/platform/linux/protocols/text-input-unstable-v3-client-protocol.h"
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

// An input method that records the state committed to it.
class FakeInputMethod : public InputMethod {
 public:
  struct State {
    std::string text;
    int32_t cursor = 0;
    int32_t anchor = 0;
    bool from_input_method = false;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
  };

  void Enable() override {
    enabled = true;
    enable_count++;
  }
  void Disable() override { enabled = false; }
  void SetSurroundingText(const std::string& text,
                          int32_t cursor,
                          int32_t anchor,
                          bool from_input_method) override {
    state.text = text;
    state.cursor = cursor;
    state.anchor = anchor;
    state.from_input_method = from_input_method;
  }
  void SetContentType(const ContentType& type) override { content_type = type; }
  void SetCursorRectangle(int32_t x,
                          int32_t y,
                          int32_t width,
                          int32_t height) override {
    state.x = x;
    state.y = y;
    state.width = width;
    state.height = height;
  }
  void Commit() override { commit_count++; }

  bool enabled = false;
  int enable_count = 0;
  int commit_count = 0;
  ContentType content_type;
  State state;
};

// Returns the editing state in the last message sent on the channel.
const rapidjson::Value& LastState(
    const FakePlatformMessenger& messenger,
    std::unique_ptr<MethodCall<rapidjson::Document>>* call) {
  *call = DecodeCall(messenger, messenger.sent.size() - 1);
  EXPECT_EQ((*call)->method_name(), "TextInputClient.updateEditingState");
  return (*(*call)->arguments())[1];
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

TEST(TextInputPluginTest, InputMethodEnabledWithClient) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  FakeInputMethod input_method;
  plugin.OnInputMethodEnter(&input_method);
  EXPECT_FALSE(input_method.enabled);

  messenger.ReceiveJsonMethodCall(
      kChannel, "TextInput.setClient",
      R"([1, {"inputType": {"name": "TextInputType.emailAddress"},
              "autocorrect": false, "enableSuggestions": false}])");
  EXPECT_TRUE(input_method.enabled);
  EXPECT_EQ(input_method.commit_count, 1);
  EXPECT_EQ(input_method.content_type.purpose,
            static_cast<uint32_t>(ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_EMAIL));
  EXPECT_EQ(input_method.content_type.hint, 0u);

  messenger.ReceiveJsonMethodCall(
      kChannel, "TextInput.setEditingState",
      R"({"text": "h\u00e9llo", "selectionBase": 2, "selectionExtent": 4})");
  EXPECT_EQ(input_method.commit_count, 2);
  EXPECT_EQ(input_method.state.text, "h\xc3\xa9llo");
  EXPECT_EQ(input_method.state.anchor, 3);
  EXPECT_EQ(input_method.state.cursor, 5);
  EXPECT_FALSE(input_method.state.from_input_method);

  messenger.ReceiveJsonMethodCall(kChannel, "TextInput.clearClient", "null");
  EXPECT_FALSE(input_method.enabled);
  EXPECT_EQ(input_method.commit_count, 3);
}

TEST(TextInputPluginTest, InputMethodEnabledOnEnter) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  messenger.ReceiveJsonMethodCall(
      kChannel, "TextInput.setClient",
      R"([1, {"inputType": {"name": "TextInputType.multiline"},
              "obscureText": true}])");
  FakeInputMethod input_method;
  plugin.OnInputMethodEnter(&input_method);
  EXPECT_TRUE(input_method.enabled);
  EXPECT_EQ(input_method.content_type.purpose,
            static_cast<uint32_t>(ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD));
  EXPECT_EQ(
      input_method.content_type.hint,
      static_cast<uint32_t>(ZWP_TEXT_INPUT_V3_CONTENT_HINT_MULTILINE |
                            ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT |
                            ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA));
}

TEST(TextInputPluginTest, InputMethodPreeditAndCommit) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  FakeInputMethod input_method;
  plugin.OnInputMethodEnter(&input_method);
  messenger.ReceiveJsonMethodCall(kChannel, "TextInput.setClient",
                                  R"([1, {}])");
  messenger.ReceiveJsonMethodCall(
      kChannel, "TextInput.setEditingState",
      R"({"text": "ab", "selectionBase": 1, "selectionExtent": 1})");

  // Compose two characters with the cursor between them.
  InputMethodChange preedit;
  preedit.preedit = "\xe3\x81\x8b\xe3\x81\xaa";
  preedit.preedit_cursor_begin = 3;
  preedit.preedit_cursor_end = 3;
  plugin.OnInputMethodChange(preedit);
  std::unique_ptr<MethodCall<rapidjson::Document>> call;
  const rapidjson::Value& state = LastState(messenger, &call);
  EXPECT_STREQ(state["text"].GetString(),
               "a\xe3\x81\x8b\xe3\x81\xaa"
               "b");
  EXPECT_EQ(state["selectionBase"].GetInt(), 2);
  EXPECT_EQ(state["composingBase"].GetInt(), 1);
  EXPECT_EQ(state["composingExtent"].GetInt(), 3);

  // The surrounding text excludes the composing text.
  EXPECT_EQ(input_method.state.text, "ab");
  EXPECT_EQ(input_method.state.cursor, 1);
  EXPECT_TRUE(input_method.state.from_input_method);

  // Commit a different string, replacing the composing text.
  InputMethodChange commit;
  commit.commit = "\xe6\xbc\xa2";
  plugin.OnInputMethodChange(commit);
  const rapidjson::Value& committed = LastState(messenger, &call);
  EXPECT_STREQ(committed["text"].GetString(),
               "a\xe6\xbc\xa2"
               "b");
  EXPECT_EQ(committed["selectionBase"].GetInt(), 2);
  EXPECT_EQ(committed["composingBase"].GetInt(), -1);
  EXPECT_EQ(input_method.state.text,
            "a\xe6\xbc\xa2"
            "b");
  EXPECT_EQ(input_method.state.cursor, 4);
}

TEST(TextInputPluginTest, InputMethodDeleteSurrounding) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  FakeInputMethod input_method;
  plugin.OnInputMethodEnter(&input_method);
  messenger.ReceiveJsonMethodCall(kChannel, "TextInput.setClient",
                                  R"([1, {}])");
  messenger.ReceiveJsonMethodCall(
      kChannel, "TextInput.setEditingState",
      R"({"text": "x\u00e9yz", "selectionBase": 2, "selectionExtent": 2})");

  // Delete "é" (two bytes) and "y", then insert "w".
  InputMethodChange change;
  change.delete_before = 2;
  change.delete_after = 1;
  change.commit = "w";
  plugin.OnInputMethodChange(change);
  std::unique_ptr<MethodCall<rapidjson::Document>> call;
  const rapidjson::Value& state = LastState(messenger, &call);
  EXPECT_STREQ(state["text"].GetString(), "xwz");
  EXPECT_EQ(state["selectionBase"].GetInt(), 2);
}

TEST(TextInputPluginTest, InputMethodDeltas) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  FakeInputMethod input_method;
  plugin.OnInputMethodEnter(&input_method);
  messenger.ReceiveJsonMethodCall(kChannel, "TextInput.setClient",
                                  R"([1, {"enableDeltaModel": true}])");
  messenger.ReceiveJsonMethodCall(
      kChannel, "TextInput.setEditingState",
      R"({"text": "abc", "selectionBase": 3, "selectionExtent": 3})");

  InputMethodChange change;
  change.delete_before = 1;
  change.commit = "d";
  plugin.OnInputMethodChange(change);
  auto call = DecodeCall(messenger, messenger.sent.size() - 1);
  EXPECT_EQ(call->method_name(),
            "TextInputClient.updateEditingStateWithDeltas");
  const rapidjson::Value& delta = (*call->arguments())[1]["deltas"][0];
  EXPECT_STREQ(delta["oldText"].GetString(), "abc");
  EXPECT_STREQ(delta["deltaText"].GetString(), "d");
  EXPECT_EQ(delta["deltaStart"].GetInt(), 2);
  EXPECT_EQ(delta["deltaEnd"].GetInt(), 3);
}

TEST(TextInputPluginTest, InputMethodStaleChange) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  FakeInputMethod input_method;
  plugin.OnInputMethodEnter(&input_method);
  messenger.ReceiveJsonMethodCall(kChannel, "TextInput.setClient",
                                  R"([1, {}])");
  int commits = input_method.commit_count;

  // A change made before the input method saw the latest state is applied,
  // but the input method isn't updated until it catches up.
  InputMethodChange stale;
  stale.commit = "a";
  stale.current = false;
  plugin.OnInputMethodChange(stale);
  EXPECT_EQ(messenger.sent.size(), 1u);
  EXPECT_EQ(input_method.commit_count, commits);

  InputMethodChange current;
  plugin.OnInputMethodChange(current);
  EXPECT_EQ(input_method.commit_count, commits + 1);
  EXPECT_EQ(input_method.state.text, "a");

  // Nothing is sent if nothing changed.
  plugin.OnInputMethodChange(current);
  EXPECT_EQ(input_method.commit_count, commits + 1);
}

TEST(TextInputPluginTest, InputMethodLeaveRemovesPreedit) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  FakeInputMethod input_method;
  plugin.OnInputMethodEnter(&input_method);
  messenger.ReceiveJsonMethodCall(kChannel, "TextInput.setClient",
                                  R"([1, {}])");
  InputMethodChange preedit;
  preedit.preedit = "abc";
  plugin.OnInputMethodChange(preedit);

  plugin.OnInputMethodLeave();
  std::unique_ptr<MethodCall<rapidjson::Document>> call;
  const rapidjson::Value& state = LastState(messenger, &call);
  EXPECT_STREQ(state["text"].GetString(), "");
  EXPECT_EQ(state["composingBase"].GetInt(), -1);

  // Changes are ignored without an input method.
  size_t sent = messenger.sent.size();
  plugin.OnInputMethodChange(preedit);
  EXPECT_EQ(messenger.sent.size(), sent);
}

TEST(TextInputPluginTest, InputMethodCursorRectangle) {
  FakePlatformMessenger messenger;
  TextInputPlugin plugin(&messenger);
  FakeInputMethod input_method;
  plugin.OnInputMethodEnter(&input_method);
  messenger.ReceiveJsonMethodCall(kChannel, "TextInput.setClient",
                                  R"([1, {}])");
  int commits = input_method.commit_count;

  messenger.ReceiveJsonMethodCall(kChannel,
                                  "TextInput.setEditableSizeAndTransform",
                                  R"({"width": 100, "height": 20,
          "transform": [2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, 10, 20, 0, 1]})");
  messenger.ReceiveJsonMethodCall(
      kChannel, "TextInput.setMarkedTextRect",
      R"({"x": 5, "y": 1, "width": 2.5, "height": 15})");
  EXPECT_EQ(input_method.state.x, 20);
  EXPECT_EQ(input_method.state.y, 22);
  EXPECT_EQ(input_method.state.width, 5);
  EXPECT_EQ(input_method.state.height, 30);
  EXPECT_EQ(input_method.commit_count, commits + 2);

  // An unchanged rectangle isn't sent again.
  messenger.ReceiveJsonMethodCall(
      kChannel, "TextInput.setMarkedTextRect",
      R"({"x": 5, "y": 1, "width": 2.5, "height": 15})");
  EXPECT_EQ(input_method.commit_count, commits + 2);
}

}  // namespace testing
}  // namespace flutter
