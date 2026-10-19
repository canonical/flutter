// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/plugin_messenger.h"

#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "flutter/shell/platform/common/client_wrapper/include/flutter/method_channel.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_method_codec.h"
#include "flutter/shell/platform/linux/fake_platform_messenger.h"
#include "flutter/shell/platform/linux/public/flutter_linux.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

constexpr char kChannel[] = "test/channel";

// A PluginMessenger forwarding to a fake engine, used through the public
// C++ messenger.
class PluginMessengerTest : public ::testing::Test {
 protected:
  PluginMessengerTest()
      : plugin_messenger_(new PluginMessenger(&engine_messenger_, &loop_)),
        messenger_(
            reinterpret_cast<FlutterLinuxMessenger*>(plugin_messenger_)) {}

  ~PluginMessengerTest() override {
    plugin_messenger_->Detach();
    plugin_messenger_->Release();
  }

  // A response to a message from the framework.
  struct Response {
    bool sent = false;
    std::vector<uint8_t> data;
  };
  // Sends a message from the framework on |channel|.
  std::shared_ptr<Response> Receive(const std::string& channel,
                                    const std::vector<uint8_t>& message) {
    auto response = std::make_shared<Response>();
    auto it = engine_messenger_.handlers.find(channel);
    if (it != engine_messenger_.handlers.end()) {
      it->second(message.data(), message.size(),
                 [response](const std::vector<uint8_t>& data) {
                   response->sent = true;
                   response->data = data;
                 });
    }
    return response;
  }

  std::vector<uint8_t> EncodeCall(const std::string& method) {
    MethodCall<EncodableValue> call(method, nullptr);
    return *StandardMethodCodec::GetInstance().EncodeMethodCall(call);
  }

  EventLoop loop_;
  FakePlatformMessenger engine_messenger_;
  PluginMessenger* plugin_messenger_;
  FlutterDesktopMessenger messenger_;
};

}  // namespace

TEST_F(PluginMessengerTest, MethodChannelHandlesCalls) {
  MethodChannel<EncodableValue> channel(&messenger_, kChannel,
                                        &StandardMethodCodec::GetInstance());
  channel.SetMethodCallHandler(
      [](const MethodCall<EncodableValue>& call,
         std::unique_ptr<MethodResult<EncodableValue>> result) {
        result->Success(EncodableValue(call.method_name() + "!"));
      });

  auto response = Receive(kChannel, EncodeCall("hello"));
  ASSERT_TRUE(response->sent);
  EXPECT_EQ(response->data,
            *StandardMethodCodec::GetInstance().EncodeSuccessEnvelope(
                std::make_unique<EncodableValue>("hello!").get()));

  channel.SetMethodCallHandler(nullptr);
  EXPECT_EQ(engine_messenger_.handlers.count(kChannel), 0u);
}

TEST_F(PluginMessengerTest, RespondsFromOtherThread) {
  std::unique_ptr<MethodResult<EncodableValue>> pending_result;
  MethodChannel<EncodableValue> channel(&messenger_, kChannel,
                                        &StandardMethodCodec::GetInstance());
  channel.SetMethodCallHandler(
      [&pending_result](const MethodCall<EncodableValue>& call,
                        std::unique_ptr<MethodResult<EncodableValue>> result) {
        pending_result = std::move(result);
      });

  auto response = Receive(kChannel, EncodeCall("hello"));
  EXPECT_FALSE(response->sent);

  std::thread thread([&pending_result]() {
    pending_result->Success(EncodableValue(42));
    pending_result.reset();
  });
  thread.join();
  EXPECT_FALSE(response->sent);

  loop_.Iterate(true);
  ASSERT_TRUE(response->sent);
  EXPECT_EQ(response->data,
            *StandardMethodCodec::GetInstance().EncodeSuccessEnvelope(
                std::make_unique<EncodableValue>(42).get()));
}

TEST_F(PluginMessengerTest, DroppedResponseSendsEmptyResponse) {
  messenger_.SetMessageHandler(
      kChannel, [](const uint8_t* message, size_t size, BinaryReply reply) {});
  auto response = Receive(kChannel, {1, 2, 3});
  ASSERT_TRUE(response->sent);
  EXPECT_TRUE(response->data.empty());
}

TEST_F(PluginMessengerTest, SendReceivesReply) {
  std::optional<std::vector<uint8_t>> reply;
  uint8_t message[] = {1, 2, 3};
  messenger_.Send(kChannel, message, sizeof(message),
                  [&reply](const uint8_t* data, size_t size) {
                    reply = std::vector<uint8_t>(data, data + size);
                  });
  ASSERT_EQ(engine_messenger_.sent.size(), 1u);
  EXPECT_EQ(engine_messenger_.sent[0].channel, kChannel);
  EXPECT_EQ(engine_messenger_.sent[0].message, (std::vector<uint8_t>{1, 2, 3}));
  EXPECT_FALSE(reply.has_value());

  uint8_t reply_data[] = {4, 5};
  engine_messenger_.sent[0].reply(reply_data, sizeof(reply_data));
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(*reply, (std::vector<uint8_t>{4, 5}));
}

TEST_F(PluginMessengerTest, SendFromOtherThread) {
  std::thread thread([this]() {
    uint8_t message[] = {1};
    messenger_.Send(kChannel, message, sizeof(message));
  });
  thread.join();
  EXPECT_TRUE(engine_messenger_.sent.empty());

  loop_.Iterate(true);
  ASSERT_EQ(engine_messenger_.sent.size(), 1u);
  EXPECT_EQ(engine_messenger_.sent[0].message, (std::vector<uint8_t>{1}));
}

TEST_F(PluginMessengerTest, DetachRepliesToPendingSends) {
  int reply_count = 0;
  const uint8_t* reply_data = reinterpret_cast<const uint8_t*>(1);
  messenger_.Send(kChannel, nullptr, 0, [&](const uint8_t* data, size_t size) {
    reply_count++;
    reply_data = data;
  });
  plugin_messenger_->Detach();
  EXPECT_EQ(reply_count, 1);
  EXPECT_EQ(reply_data, nullptr);

  // A late reply from the engine is ignored.
  engine_messenger_.sent[0].reply(nullptr, 0);
  EXPECT_EQ(reply_count, 1);

  // Sends after detaching reply immediately.
  messenger_.Send(kChannel, nullptr, 0,
                  [&](const uint8_t* data, size_t size) { reply_count++; });
  EXPECT_EQ(reply_count, 2);
  EXPECT_EQ(engine_messenger_.sent.size(), 1u);
}

TEST_F(PluginMessengerTest, DetachRemovesHandlers) {
  bool destroyed = false;
  struct DestroyFlag {
    explicit DestroyFlag(bool* destroyed) : destroyed(destroyed) {}
    ~DestroyFlag() { *destroyed = true; }
    bool* destroyed;
  };
  auto flag = std::make_shared<DestroyFlag>(&destroyed);
  BinaryReply pending_reply;
  messenger_.SetMessageHandler(
      kChannel, [flag = std::move(flag), &pending_reply](
                    const uint8_t* message, size_t size, BinaryReply reply) {
        pending_reply = std::move(reply);
      });
  auto response = Receive(kChannel, {1});
  EXPECT_FALSE(response->sent);

  plugin_messenger_->Detach();
  EXPECT_TRUE(destroyed);
  EXPECT_EQ(engine_messenger_.handlers.count(kChannel), 0u);

  // Responding after detaching is safe and ignored.
  uint8_t data[] = {1};
  pending_reply(data, sizeof(data));
  pending_reply = nullptr;
  EXPECT_FALSE(response->sent);
}

TEST_F(PluginMessengerTest, ResponseOutlivesMessenger) {
  BinaryReply pending_reply;
  messenger_.SetMessageHandler(
      kChannel,
      [&pending_reply](const uint8_t* message, size_t size, BinaryReply reply) {
        pending_reply = std::move(reply);
      });
  Receive(kChannel, {1});

  // The response holds a reference to the messenger.
  plugin_messenger_->Detach();
  plugin_messenger_->Release();
  plugin_messenger_ = new PluginMessenger(&engine_messenger_, &loop_);

  pending_reply(nullptr, 0);
  pending_reply = nullptr;
}

}  // namespace testing
}  // namespace flutter
