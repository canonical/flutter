// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/platform_view_manager.h"

#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_method_codec.h"
#include "flutter/shell/platform/linux/fake_platform_messenger.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

constexpr char kChannel[] = "flutter/platform_views";

std::vector<uint8_t> Call(FakePlatformMessenger& messenger,
                          const std::string& method,
                          EncodableValue args) {
  MethodCall<EncodableValue> call(method,
                                  std::make_unique<EncodableValue>(args));
  return messenger.Receive(
      kChannel, *StandardMethodCodec::GetInstance().EncodeMethodCall(call));
}

std::vector<uint8_t> Create(FakePlatformMessenger& messenger,
                            int64_t id,
                            const std::string& view_type,
                            std::vector<uint8_t> params = {}) {
  return Call(messenger, "create",
              EncodableValue(EncodableMap{
                  {EncodableValue("id"), EncodableValue(id)},
                  {EncodableValue("viewType"), EncodableValue(view_type)},
                  {EncodableValue("width"), EncodableValue(100.0)},
                  {EncodableValue("height"), EncodableValue(50.0)},
                  {EncodableValue("params"), EncodableValue(params)},
              }));
}

bool IsSuccess(const std::vector<uint8_t>& response) {
  return response ==
         *StandardMethodCodec::GetInstance().EncodeSuccessEnvelope();
}

// Counts how many times it is destroyed.
struct DestroyCounter {
  explicit DestroyCounter(int* count) : count(count) {}
  ~DestroyCounter() { (*count)++; }
  int* count;
};

}  // namespace

TEST(PlatformViewManagerTest, CreateAndDispose) {
  FakePlatformMessenger messenger;
  PlatformViewManager manager(&messenger, nullptr, nullptr);
  std::vector<uint8_t> received_params;
  int destroyed = 0;
  manager.RegisterViewType(
      "test", [&](LinuxPlatformView* view, const std::vector<uint8_t>& params) {
        received_params = params;
        auto counter = std::make_shared<DestroyCounter>(&destroyed);
        view->SetCallbacks([counter](int, int, double) {}, nullptr);
        return true;
      });

  EXPECT_TRUE(IsSuccess(Create(messenger, 7, "test", {1, 2, 3})));
  EXPECT_EQ(received_params, (std::vector<uint8_t>{1, 2, 3}));
  ASSERT_NE(manager.GetView(7), nullptr);
  EXPECT_EQ(manager.GetView(7)->id(), 7);
  EXPECT_EQ(destroyed, 0);

  EXPECT_TRUE(IsSuccess(Call(messenger, "dispose", EncodableValue(7))));
  EXPECT_EQ(manager.GetView(7), nullptr);
  EXPECT_EQ(destroyed, 1);
}

TEST(PlatformViewManagerTest, UnknownViewType) {
  FakePlatformMessenger messenger;
  PlatformViewManager manager(&messenger, nullptr, nullptr);
  EXPECT_FALSE(IsSuccess(Create(messenger, 1, "missing")));
  EXPECT_EQ(manager.GetView(1), nullptr);
}

TEST(PlatformViewManagerTest, FactoryFails) {
  FakePlatformMessenger messenger;
  PlatformViewManager manager(&messenger, nullptr, nullptr);
  int destroyed = 0;
  manager.RegisterViewType(
      "test", [&](LinuxPlatformView* view, const std::vector<uint8_t>& params) {
        auto counter = std::make_shared<DestroyCounter>(&destroyed);
        view->SetCallbacks(nullptr, [counter](const auto&) {});
        return false;
      });
  EXPECT_FALSE(IsSuccess(Create(messenger, 1, "test")));
  EXPECT_EQ(manager.GetView(1), nullptr);
  EXPECT_EQ(destroyed, 1);
}

TEST(PlatformViewManagerTest, DuplicateId) {
  FakePlatformMessenger messenger;
  PlatformViewManager manager(&messenger, nullptr, nullptr);
  manager.RegisterViewType(
      "test",
      [](LinuxPlatformView*, const std::vector<uint8_t>&) { return true; });
  EXPECT_TRUE(IsSuccess(Create(messenger, 1, "test")));
  EXPECT_FALSE(IsSuccess(Create(messenger, 1, "test")));
}

TEST(PlatformViewManagerTest, PointerEvent) {
  FakePlatformMessenger messenger;
  PlatformViewManager manager(&messenger, nullptr, nullptr);
  std::vector<PlatformViewPointerEvent> events;
  manager.RegisterViewType(
      "test", [&](LinuxPlatformView* view, const std::vector<uint8_t>&) {
        view->SetCallbacks(nullptr, [&](const PlatformViewPointerEvent& e) {
          events.push_back(e);
        });
        return true;
      });
  ASSERT_TRUE(IsSuccess(Create(messenger, 3, "test")));

  EXPECT_TRUE(IsSuccess(
      Call(messenger, "pointer",
           EncodableValue(EncodableMap{
               {EncodableValue("id"), EncodableValue(3)},
               {EncodableValue("kind"), EncodableValue("down")},
               {EncodableValue("deviceKind"), EncodableValue("touch")},
               {EncodableValue("device"), EncodableValue(2)},
               {EncodableValue("buttons"), EncodableValue(1)},
               {EncodableValue("x"), EncodableValue(1.5)},
               {EncodableValue("y"), EncodableValue(2.5)},
               {EncodableValue("timestamp"), EncodableValue(int64_t{1234})},
           }))));
  EXPECT_TRUE(IsSuccess(
      Call(messenger, "pointer",
           EncodableValue(EncodableMap{
               {EncodableValue("id"), EncodableValue(3)},
               {EncodableValue("kind"), EncodableValue("scroll")},
               {EncodableValue("scrollDeltaY"), EncodableValue(-10.0)},
           }))));
  // Unknown views are ignored.
  EXPECT_TRUE(
      IsSuccess(Call(messenger, "pointer",
                     EncodableValue(EncodableMap{
                         {EncodableValue("id"), EncodableValue(4)},
                         {EncodableValue("kind"), EncodableValue("down")},
                     }))));

  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].kind, PlatformViewPointerEvent::Kind::kDown);
  EXPECT_EQ(events[0].device_kind,
            PlatformViewPointerEvent::DeviceKind::kTouch);
  EXPECT_EQ(events[0].device, 2);
  EXPECT_EQ(events[0].buttons, 1);
  EXPECT_EQ(events[0].x, 1.5);
  EXPECT_EQ(events[0].y, 2.5);
  EXPECT_EQ(events[0].timestamp_us, 1234);
  EXPECT_EQ(events[1].kind, PlatformViewPointerEvent::Kind::kScroll);
  EXPECT_EQ(events[1].device_kind,
            PlatformViewPointerEvent::DeviceKind::kMouse);
  EXPECT_EQ(events[1].scroll_delta_y, -10.0);
}

TEST(PlatformViewManagerTest, DisposeAll) {
  FakePlatformMessenger messenger;
  int destroyed = 0;
  {
    PlatformViewManager manager(&messenger, nullptr, nullptr);
    manager.RegisterViewType(
        "test", [&](LinuxPlatformView* view, const std::vector<uint8_t>&) {
          auto counter = std::make_shared<DestroyCounter>(&destroyed);
          view->SetCallbacks([counter](int, int, double) {}, nullptr);
          return true;
        });
    Create(messenger, 1, "test");
    Create(messenger, 2, "test");
    manager.DisposeAll();
    EXPECT_EQ(destroyed, 2);
    EXPECT_EQ(manager.GetView(1), nullptr);
    Create(messenger, 3, "test");
  }
  // Views are disposed with the manager.
  EXPECT_EQ(destroyed, 3);
}

}  // namespace testing
}  // namespace flutter
