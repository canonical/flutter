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

// A platform view that shows buffers nowhere, with the buffer use tracking
// exposed for testing.
class FakePlatformView : public LinuxPlatformView {
 public:
  explicit FakePlatformView(int64_t id) : LinuxPlatformView(id, nullptr) {}

  int presents = 0;

  // Marks the current buffer as used, returning its serial.
  uint64_t UseCurrent() {
    std::scoped_lock lock(mutex_);
    if (current_ == nullptr) {
      return 0;
    }
    AddUse(current_);
    return current_->serial();
  }

  void EndUse(uint64_t serial) {
    std::function<void()> release;
    {
      std::scoped_lock lock(mutex_);
      if (Buffer* buffer = FindBuffer(serial)) {
        release = RemoveUse(buffer);
      }
    }
    if (release) {
      release();
    }
  }

 protected:
  class FakeBuffer : public Buffer {
   public:
    FakeBuffer(std::shared_ptr<LinuxPlatformView> view,
               const DmabufFrame& frame)
        : Buffer(std::move(view), frame) {}
  };

  std::unique_ptr<Buffer> CreateBackendBuffer(
      const DmabufFrame& frame) override {
    return std::make_unique<FakeBuffer>(shared_from_this(), frame);
  }

  void OnPresent() override { presents++; }
};

std::shared_ptr<LinuxPlatformView> CreateFakeView(int64_t id) {
  return std::make_shared<FakePlatformView>(id);
}

DmabufFrame MakeFrame(int* released) {
  DmabufFrame frame;
  frame.width = 4;
  frame.height = 2;
  frame.planes.push_back({.fd = -1, .offset = 0, .stride = 16});
  frame.release = [released]() { (*released)++; };
  return frame;
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
  PlatformViewManager manager(&messenger, CreateFakeView);
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
  PlatformViewManager manager(&messenger, CreateFakeView);
  EXPECT_FALSE(IsSuccess(Create(messenger, 1, "missing")));
  EXPECT_EQ(manager.GetView(1), nullptr);
}

TEST(PlatformViewManagerTest, FactoryFails) {
  FakePlatformMessenger messenger;
  PlatformViewManager manager(&messenger, CreateFakeView);
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
  PlatformViewManager manager(&messenger, CreateFakeView);
  manager.RegisterViewType(
      "test",
      [](LinuxPlatformView*, const std::vector<uint8_t>&) { return true; });
  EXPECT_TRUE(IsSuccess(Create(messenger, 1, "test")));
  EXPECT_FALSE(IsSuccess(Create(messenger, 1, "test")));
}

TEST(PlatformViewManagerTest, PointerEvent) {
  FakePlatformMessenger messenger;
  PlatformViewManager manager(&messenger, CreateFakeView);
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
    PlatformViewManager manager(&messenger, CreateFakeView);
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

TEST(PlatformViewManagerTest, Unsupported) {
  FakePlatformMessenger messenger;
  PlatformViewManager manager(&messenger, [](int64_t) { return nullptr; });
  bool created = false;
  manager.RegisterViewType(
      "test", [&](LinuxPlatformView*, const std::vector<uint8_t>&) {
        created = true;
        return true;
      });
  EXPECT_FALSE(IsSuccess(Create(messenger, 1, "test")));
  EXPECT_FALSE(created);
  EXPECT_EQ(manager.GetView(1), nullptr);
}

TEST(PlatformViewTest, ReleasesReplacedBuffers) {
  auto view = std::make_shared<FakePlatformView>(1);
  int released_a = 0;
  int released_b = 0;
  auto a = view->CreateBuffer(MakeFrame(&released_a));
  auto b = view->CreateBuffer(MakeFrame(&released_b));
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(a->width(), 4);
  EXPECT_NE(a->serial(), b->serial());

  view->Present(a.get());
  EXPECT_EQ(view->presents, 1);
  // Presenting the same buffer again does nothing.
  view->Present(a.get());
  EXPECT_EQ(view->presents, 1);

  // A buffer that isn't in use is released as soon as it is replaced.
  view->Present(b.get());
  EXPECT_EQ(released_a, 1);
  EXPECT_EQ(released_b, 0);

  // A buffer in use is released when its last use ends.
  uint64_t use1 = view->UseCurrent();
  uint64_t use2 = view->UseCurrent();
  view->Present(a.get());
  EXPECT_EQ(released_b, 0);
  view->EndUse(use1);
  EXPECT_EQ(released_b, 0);
  view->EndUse(use2);
  EXPECT_EQ(released_b, 1);

  // Ending a use of a freed buffer is ignored.
  uint64_t use3 = view->UseCurrent();
  a.reset();
  view->EndUse(use3);
  EXPECT_EQ(released_a, 1);

  view->Dispose();
  b.reset();
}

TEST(PlatformViewTest, DisposeReleasesCurrentBuffer) {
  auto view = std::make_shared<FakePlatformView>(1);
  int released = 0;
  auto buffer = view->CreateBuffer(MakeFrame(&released));
  view->Present(buffer.get());
  EXPECT_EQ(released, 0);
  view->Dispose();
  EXPECT_EQ(released, 1);
  // Buffers can't be presented once disposed.
  view->Present(nullptr);
  EXPECT_EQ(view->presents, 1);
}

TEST(PlatformViewTest, DisposeWaitsForUses) {
  auto view = std::make_shared<FakePlatformView>(1);
  int released = 0;
  auto buffer = view->CreateBuffer(MakeFrame(&released));
  view->Present(buffer.get());
  uint64_t use = view->UseCurrent();
  view->Dispose();
  EXPECT_EQ(released, 0);
  view->EndUse(use);
  EXPECT_EQ(released, 1);
}

TEST(PlatformViewTest, RejectsInvalidBuffers) {
  auto view = std::make_shared<FakePlatformView>(1);
  int released = 0;
  DmabufFrame frame = MakeFrame(&released);
  frame.planes.clear();
  EXPECT_EQ(view->CreateBuffer(frame), nullptr);
  frame = MakeFrame(&released);
  frame.width = 0;
  EXPECT_EQ(view->CreateBuffer(frame), nullptr);
}

}  // namespace testing
}  // namespace flutter
