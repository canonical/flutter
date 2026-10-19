// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/platform_handler.h"

#include "flutter/shell/platform/linux/fake_platform_messenger.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

constexpr char kChannel[] = "flutter/platform";

class FakeClipboard : public Clipboard {
 public:
  bool SetText(const std::string& value) override {
    text = value;
    return true;
  }
  void GetText(TextCallback callback) override {
    pending = std::move(callback);
  }
  bool HasText() const override { return text.has_value(); }

  std::optional<std::string> text;
  TextCallback pending;
};

class PlatformHandlerTest : public ::testing::Test {
 protected:
  PlatformHandlerTest()
      : handler_(
            &messenger_,
            &clipboard_,
            [this]() { exit_count_++; },
            [this]() { alert_count_++; }) {}

  std::vector<uint8_t> Call(const std::string& method,
                            const std::string& args) {
    return messenger_.ReceiveJsonMethodCall(kChannel, method, args);
  }

  // Calls |method|, storing the response in |response| when sent.
  void CallAsync(const std::string& method,
                 const std::string& args_json,
                 std::vector<uint8_t>* response) {
    auto args = std::make_unique<rapidjson::Document>();
    args->Parse(args_json.c_str());
    MethodCall<rapidjson::Document> call(method, std::move(args));
    std::vector<uint8_t> message =
        *JsonMethodCodec::GetInstance().EncodeMethodCall(call);
    messenger_.handlers[kChannel](
        message.data(), message.size(),
        [response](const std::vector<uint8_t>& r) { *response = r; });
  }

  // Decodes a success response.
  std::unique_ptr<rapidjson::Document> Result(
      const std::vector<uint8_t>& response) {
    std::unique_ptr<rapidjson::Document> envelope =
        JsonMessageCodec::GetInstance().DecodeMessage(response);
    EXPECT_TRUE(envelope && envelope->IsArray() && envelope->Size() == 1);
    auto result = std::make_unique<rapidjson::Document>();
    result->CopyFrom((*envelope)[0], result->GetAllocator());
    return result;
  }

  // Replies to the System.requestAppExit call at |index| with |response|.
  void ReplyToExitRequest(size_t index, const char* response) {
    ASSERT_LT(index, messenger_.sent.size());
    std::unique_ptr<MethodCall<rapidjson::Document>> call =
        JsonMethodCodec::GetInstance().DecodeMethodCall(
            messenger_.sent[index].message);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->method_name(), "System.requestAppExit");
    EXPECT_STREQ((*call->arguments())["type"].GetString(), "cancelable");
    rapidjson::Document result(rapidjson::kObjectType);
    result.AddMember("response", rapidjson::StringRef(response),
                     result.GetAllocator());
    std::vector<uint8_t> reply =
        *JsonMethodCodec::GetInstance().EncodeSuccessEnvelope(&result);
    messenger_.sent[index].reply(reply.data(), reply.size());
  }

  FakePlatformMessenger messenger_;
  FakeClipboard clipboard_;
  int exit_count_ = 0;
  int alert_count_ = 0;
  PlatformHandler handler_;
};

}  // namespace

TEST_F(PlatformHandlerTest, SetsClipboard) {
  Result(Call("Clipboard.setData", R"({"text": "hello"})"));
  EXPECT_EQ(clipboard_.text, "hello");
}

TEST_F(PlatformHandlerTest, GetsClipboard) {
  std::vector<uint8_t> response;
  CallAsync("Clipboard.getData", R"("text/plain")", &response);
  ASSERT_TRUE(clipboard_.pending);
  EXPECT_TRUE(response.empty());
  clipboard_.pending("pasted");
  std::unique_ptr<rapidjson::Document> result = Result(response);
  EXPECT_STREQ((*result)["text"].GetString(), "pasted");
}

TEST_F(PlatformHandlerTest, GetsEmptyClipboard) {
  std::vector<uint8_t> response;
  CallAsync("Clipboard.getData", R"("text/plain")", &response);
  ASSERT_TRUE(clipboard_.pending);
  clipboard_.pending(std::nullopt);
  EXPECT_TRUE(Result(response)->IsNull());
}

TEST_F(PlatformHandlerTest, RejectsUnknownClipboardFormat) {
  std::vector<uint8_t> response = Call("Clipboard.getData", R"("image/png")");
  std::unique_ptr<rapidjson::Document> envelope =
      JsonMessageCodec::GetInstance().DecodeMessage(response);
  ASSERT_TRUE(envelope && envelope->IsArray());
  EXPECT_STREQ((*envelope)[0].GetString(), "Unknown Clipboard Format");
}

TEST_F(PlatformHandlerTest, HasStrings) {
  EXPECT_FALSE(
      (*Result(Call("Clipboard.hasStrings", R"("text/plain")")))["value"]
          .GetBool());
  clipboard_.text = "text";
  EXPECT_TRUE(
      (*Result(Call("Clipboard.hasStrings", R"("text/plain")")))["value"]
          .GetBool());
}

TEST_F(PlatformHandlerTest, PlaysAlert) {
  Result(Call("SystemSound.play", R"("SystemSoundType.click")"));
  EXPECT_EQ(alert_count_, 0);
  Result(Call("SystemSound.play", R"("SystemSoundType.alert")"));
  EXPECT_EQ(alert_count_, 1);
}

TEST_F(PlatformHandlerTest, IgnoresSystemChrome) {
  EXPECT_FALSE(Call("SystemChrome.setApplicationSwitcherDescription",
                    R"({"label": "App", "primaryColor": 0})")
                   .empty());
}

TEST_F(PlatformHandlerTest, UnknownMethodNotImplemented) {
  EXPECT_TRUE(Call("Unknown.method", "null").empty());
}

TEST_F(PlatformHandlerTest, SystemNavigatorPopExits) {
  Result(Call("SystemNavigator.pop", "null"));
  EXPECT_EQ(exit_count_, 1);
}

TEST_F(PlatformHandlerTest, RequiredExitExits) {
  Result(Call("System.initializationComplete", "null"));
  std::unique_ptr<rapidjson::Document> result =
      Result(Call("System.exitApplication", R"({"type": "required"})"));
  EXPECT_STREQ((*result)["response"].GetString(), "exit");
  EXPECT_EQ(exit_count_, 1);
  EXPECT_TRUE(messenger_.sent.empty());
}

TEST_F(PlatformHandlerTest, CancelableExitBeforeInitializationExits) {
  std::unique_ptr<rapidjson::Document> result =
      Result(Call("System.exitApplication", R"({"type": "cancelable"})"));
  EXPECT_STREQ((*result)["response"].GetString(), "exit");
  EXPECT_EQ(exit_count_, 1);
}

TEST_F(PlatformHandlerTest, CancelableExitAsksFramework) {
  Result(Call("System.initializationComplete", "null"));

  std::vector<uint8_t> response;
  CallAsync("System.exitApplication", R"({"type": "cancelable"})", &response);
  EXPECT_TRUE(response.empty());
  ASSERT_EQ(messenger_.sent.size(), 1u);
  EXPECT_EQ(messenger_.sent[0].channel, kChannel);
  EXPECT_EQ(exit_count_, 0);

  ReplyToExitRequest(0, "cancel");
  EXPECT_STREQ((*Result(response))["response"].GetString(), "cancel");
  EXPECT_EQ(exit_count_, 0);
}

TEST_F(PlatformHandlerTest, RequestAppExitBeforeInitializationExits) {
  handler_.RequestAppExit();
  EXPECT_EQ(exit_count_, 1);
  EXPECT_TRUE(messenger_.sent.empty());
}

TEST_F(PlatformHandlerTest, RequestAppExitCancelled) {
  Result(Call("System.initializationComplete", "null"));
  handler_.RequestAppExit();
  ReplyToExitRequest(0, "cancel");
  EXPECT_EQ(exit_count_, 0);
}

TEST_F(PlatformHandlerTest, RequestAppExitAccepted) {
  Result(Call("System.initializationComplete", "null"));
  handler_.RequestAppExit();
  ReplyToExitRequest(0, "exit");
  EXPECT_EQ(exit_count_, 1);
}

TEST_F(PlatformHandlerTest, RequestAppExitUnhandledExits) {
  Result(Call("System.initializationComplete", "null"));
  handler_.RequestAppExit();
  ASSERT_EQ(messenger_.sent.size(), 1u);
  messenger_.sent[0].reply(nullptr, 0);
  EXPECT_EQ(exit_count_, 1);
}

}  // namespace testing
}  // namespace flutter
