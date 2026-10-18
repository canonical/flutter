// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests the plugin C ABI through the C++ client wrapper.

#include "flutter/shell/platform/linux/client_wrapper/include/flutter/plugin_registrar_linux.h"

#include <unistd.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "flutter/fml/unique_fd.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/method_channel.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/method_result_functions.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_method_codec.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/fake_platform_messenger.h"
#include "flutter/shell/platform/linux/flutter_desktop_messenger.h"
#include "flutter/shell/platform/linux/flutter_desktop_plugin_registrar.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

constexpr char kChannel[] = "test/channel";

class TestPlugin : public Plugin {
 public:
  explicit TestPlugin(bool* destroyed) : destroyed_(destroyed) {}
  ~TestPlugin() override { *destroyed_ = true; }

 private:
  bool* destroyed_;
};

class TestViewObserver : public ViewObserver {
 public:
  void OnViewAdded(FlutterView* view) override { added.push_back(view->id()); }
  void OnViewRemoved(FlutterView* view) override {
    removed.push_back(view->id());
  }

  std::vector<int64_t> added;
  std::vector<int64_t> removed;
};

class PluginRegistrarLinuxTest : public ::testing::Test {
 protected:
  PluginRegistrarLinuxTest()
      : messenger_(new FlutterDesktopMessenger(&platform_messenger_, &loop_)),
        core_registrar_(
            std::make_unique<FlutterDesktopPluginRegistrar>(messenger_,
                                                            &loop_,
                                                            nullptr,
                                                            &views_)) {}

  ~PluginRegistrarLinuxTest() override {
    DestroyRegistrar();
    messenger_->Detach();
    messenger_->Release();
  }

  // Gets the wrapper, as a plugin would.
  PluginRegistrarLinux* registrar() {
    return PluginRegistrarManager::GetInstance()
        ->GetRegistrar<PluginRegistrarLinux>(core_registrar_->ToRef());
  }

  // Destroys the registrar, as the engine does.
  void DestroyRegistrar() {
    if (core_registrar_) {
      core_registrar_->NotifyDestroyed();
      core_registrar_.reset();
    }
  }

  // Runs the event loop until there is nothing left to do.
  void RunPending() {
    for (int i = 0; i < 10; i++) {
      loop_.Iterate(false);
    }
  }

  EventLoop loop_;
  FakePlatformMessenger platform_messenger_;
  FlutterDesktopMessenger* messenger_;
  FlutterDesktopPluginRegistrar::Views views_;
  std::unique_ptr<FlutterDesktopPluginRegistrar> core_registrar_;
};

}  // namespace

TEST_F(PluginRegistrarLinuxTest, HandlesMethodCalls) {
  MethodChannel<EncodableValue> channel(registrar()->messenger(), kChannel,
                                        &StandardMethodCodec::GetInstance());
  channel.SetMethodCallHandler(
      [](const MethodCall<EncodableValue>& call,
         std::unique_ptr<MethodResult<EncodableValue>> result) {
        EXPECT_EQ(call.method_name(), "ping");
        result->Success(EncodableValue("pong"));
      });

  MethodCall<EncodableValue> call("ping", nullptr);
  std::vector<uint8_t> response = platform_messenger_.Receive(
      kChannel, *StandardMethodCodec::GetInstance().EncodeMethodCall(call));
  ASSERT_FALSE(response.empty());

  std::string result;
  MethodResultFunctions<EncodableValue> handler(
      [&result](const EncodableValue* value) {
        result = std::get<std::string>(*value);
      },
      nullptr, nullptr);
  StandardMethodCodec::GetInstance().DecodeAndProcessResponseEnvelope(
      response.data(), response.size(), &handler);
  EXPECT_EQ(result, "pong");
}

TEST_F(PluginRegistrarLinuxTest, InvokesMethods) {
  MethodChannel<EncodableValue> channel(registrar()->messenger(), kChannel,
                                        &StandardMethodCodec::GetInstance());
  std::string result;
  channel.InvokeMethod("hello", std::make_unique<EncodableValue>(42),
                       std::make_unique<MethodResultFunctions<EncodableValue>>(
                           [&result](const EncodableValue* value) {
                             result = std::get<std::string>(*value);
                           },
                           nullptr, nullptr));

  ASSERT_EQ(platform_messenger_.sent.size(), 1u);
  EXPECT_EQ(platform_messenger_.sent[0].channel, kChannel);
  std::unique_ptr<MethodCall<EncodableValue>> call =
      StandardMethodCodec::GetInstance().DecodeMethodCall(
          platform_messenger_.sent[0].message);
  EXPECT_EQ(call->method_name(), "hello");
  EXPECT_EQ(std::get<int32_t>(*call->arguments()), 42);

  std::unique_ptr<std::vector<uint8_t>> reply =
      StandardMethodCodec::GetInstance().EncodeSuccessEnvelope(
          std::make_unique<EncodableValue>("hi").get());
  platform_messenger_.sent[0].reply(reply->data(), reply->size());
  EXPECT_EQ(result, "hi");
}

TEST_F(PluginRegistrarLinuxTest, SendsFromOtherThreads) {
  BinaryMessenger* messenger = registrar()->messenger();
  std::thread thread([messenger]() {
    uint8_t data[] = {1, 2, 3};
    messenger->Send(kChannel, data, sizeof(data));
  });
  thread.join();
  EXPECT_TRUE(platform_messenger_.sent.empty());

  RunPending();
  ASSERT_EQ(platform_messenger_.sent.size(), 1u);
  EXPECT_EQ(platform_messenger_.sent[0].message,
            std::vector<uint8_t>({1, 2, 3}));
}

TEST_F(PluginRegistrarLinuxTest, RespondsFromOtherThreads) {
  std::thread thread;
  registrar()->messenger()->SetMessageHandler(
      kChannel,
      [&thread](const uint8_t* message, size_t size, BinaryReply reply) {
        thread = std::thread([reply = std::move(reply)]() {
          uint8_t data[] = {4, 5};
          reply(data, sizeof(data));
        });
      });

  std::vector<uint8_t> response;
  platform_messenger_.handlers[kChannel](
      nullptr, 0, [&response](const std::vector<uint8_t>& r) { response = r; });
  thread.join();
  EXPECT_TRUE(response.empty());

  RunPending();
  EXPECT_EQ(response, std::vector<uint8_t>({4, 5}));
}

TEST_F(PluginRegistrarLinuxTest, DetachRemovesHandlers) {
  registrar()->messenger()->SetMessageHandler(
      kChannel, [](const uint8_t* message, size_t size, BinaryReply reply) {
        // Never responds.
      });
  EXPECT_EQ(platform_messenger_.handlers.count(kChannel), 1u);

  // The pending response is dropped, which makes the engine send an empty
  // reply.
  auto state = std::make_shared<bool>(false);
  std::weak_ptr<bool> weak_state = state;
  platform_messenger_.handlers[kChannel](
      nullptr, 0, [state](const std::vector<uint8_t>& r) { *state = true; });
  state.reset();
  EXPECT_FALSE(weak_state.expired());

  messenger_->Detach();
  EXPECT_TRUE(platform_messenger_.handlers.empty());
  EXPECT_TRUE(weak_state.expired());
  EXPECT_FALSE(FlutterDesktopMessengerIsAvailable(messenger_));
}

TEST_F(PluginRegistrarLinuxTest, DestroysPluginsWithRegistrar) {
  bool destroyed = false;
  registrar()->AddPlugin(std::make_unique<TestPlugin>(&destroyed));
  EXPECT_FALSE(destroyed);
  DestroyRegistrar();
  EXPECT_TRUE(destroyed);
}

TEST_F(PluginRegistrarLinuxTest, TracksViews) {
  // The views are never dereferenced.
  int view0;
  int view1;
  views_[0] = reinterpret_cast<FlutterLinuxView*>(&view0);

  PluginRegistrarLinux* plugin_registrar = registrar();
  ASSERT_NE(plugin_registrar->GetViewById(0), nullptr);
  EXPECT_EQ(plugin_registrar->GetViewById(0)->id(), 0);
  EXPECT_EQ(plugin_registrar->GetViewById(1), nullptr);

  TestViewObserver observer;
  plugin_registrar->AddViewObserver(&observer);

  views_[1] = reinterpret_cast<FlutterLinuxView*>(&view1);
  core_registrar_->OnViewAdded(1);
  EXPECT_EQ(observer.added, std::vector<int64_t>({1}));
  ASSERT_NE(plugin_registrar->GetViewById(1), nullptr);

  core_registrar_->OnViewRemoved(1);
  views_.erase(1);
  EXPECT_EQ(observer.removed, std::vector<int64_t>({1}));
  EXPECT_EQ(plugin_registrar->GetViewById(1), nullptr);

  plugin_registrar->RemoveViewObserver(&observer);
  core_registrar_->OnViewRemoved(0);
  EXPECT_EQ(observer.removed, std::vector<int64_t>({1}));
}

TEST_F(PluginRegistrarLinuxTest, PostsTasks) {
  std::vector<int> ran;
  registrar()->PostTask([&ran]() { ran.push_back(1); });
  std::thread thread([this, &ran]() {
    registrar()->PostTask([&ran]() { ran.push_back(2); });
  });
  thread.join();
  EXPECT_TRUE(ran.empty());

  RunPending();
  EXPECT_EQ(ran, std::vector<int>({1, 2}));
}

TEST_F(PluginRegistrarLinuxTest, DropsTasksWhenDestroyed) {
  auto state = std::make_shared<bool>(false);
  std::weak_ptr<bool> weak_state = state;
  registrar()->PostTask([state]() { *state = true; });
  state.reset();
  EXPECT_FALSE(weak_state.expired());

  DestroyRegistrar();
  RunPending();
  EXPECT_TRUE(weak_state.expired());
}

TEST_F(PluginRegistrarLinuxTest, WatchesFileDescriptors) {
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  fml::UniqueFD read_fd(fds[0]);
  fml::UniqueFD write_fd(fds[1]);

  std::vector<uint32_t> events;
  auto state = std::make_shared<int>(0);
  std::weak_ptr<int> weak_state = state;
  uint64_t id =
      registrar()->AddFdWatch(read_fd.get(), PluginRegistrarLinux::kFdReadable,
                              [&events, &read_fd, state](uint32_t e) {
                                char c;
                                EXPECT_EQ(read(read_fd.get(), &c, 1), 1);
                                events.push_back(e);
                              });
  EXPECT_NE(id, 0u);
  state.reset();

  RunPending();
  EXPECT_TRUE(events.empty());

  EXPECT_EQ(write(write_fd.get(), "x", 1), 1);
  RunPending();
  EXPECT_EQ(events, std::vector<uint32_t>({PluginRegistrarLinux::kFdReadable}));

  // No events are delivered when none are requested.
  EXPECT_TRUE(registrar()->SetFdWatchEvents(id, 0));
  EXPECT_EQ(write(write_fd.get(), "x", 1), 1);
  RunPending();
  EXPECT_EQ(events.size(), 1u);

  // The watch is removed with the registrar.
  EXPECT_FALSE(weak_state.expired());
  DestroyRegistrar();
  EXPECT_TRUE(weak_state.expired());
}

TEST_F(PluginRegistrarLinuxTest, RemovesFdWatches) {
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  fml::UniqueFD read_fd(fds[0]);
  fml::UniqueFD write_fd(fds[1]);

  int count = 0;
  uint64_t id = 0;
  id =
      registrar()->AddFdWatch(write_fd.get(), PluginRegistrarLinux::kFdWritable,
                              [this, &count, &id](uint32_t events) {
                                count++;
                                registrar()->RemoveFdWatch(id);
                              });
  RunPending();
  EXPECT_EQ(count, 1);
}

}  // namespace testing
}  // namespace flutter
