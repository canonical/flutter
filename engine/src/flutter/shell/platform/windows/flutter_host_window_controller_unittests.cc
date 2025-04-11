// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/flutter_host_window_controller.h"
#include "flutter/shell/platform/windows/testing/flutter_windows_engine_builder.h"
#include "flutter/shell/platform/windows/testing/windows_test.h"
#include "gtest/gtest.h"

namespace flutter {

struct WindowingInitRequest {
  void (*on_message)(WindowsMessage*);
};

struct WindowCreationRequest {
  double width;
  double height;
  double min_width;
  double min_height;
  double max_width;
  double max_height;
};

extern "C" {
FLUTTER_EXPORT
void flutter_windowing_initialize(int64_t engine_id,
                                  const flutter::WindowingInitRequest* request);

FLUTTER_EXPORT
bool flutter_windowing_has_top_level_windows(int64_t engine_id);

FLUTTER_EXPORT
int64_t flutter_create_regular_window(
    int64_t engine_id,
    const flutter::WindowCreationRequest* request);

FLUTTER_EXPORT
HWND flutter_get_window_handle(int64_t engine_id, FlutterViewId view_id);

FLUTTER_EXPORT
void flutter_get_window_size(HWND hwnd, Size* size);

FLUTTER_EXPORT
int64_t flutter_get_window_state(HWND hwnd);

FLUTTER_EXPORT
void flutter_set_window_state(HWND hwnd, int64_t state);

FLUTTER_EXPORT
void flutter_set_window_size(HWND hwnd, double width, double height);
}

namespace testing {

namespace {

class FlutterHostWindowControllerTest : public WindowsTest {
 public:
  FlutterHostWindowControllerTest() = default;
  virtual ~FlutterHostWindowControllerTest() = default;

 protected:
  void SetUp() override {
    auto& context = GetContext();
    FlutterWindowsEngineBuilder builder(context);
    builder.SetSwitches({"--enable-multi-window=true"});

    engine_ = builder.Build();
    ASSERT_TRUE(engine_);

    engine_->SetRootIsolateCreateCallback(context.GetRootIsolateCallback());
    ASSERT_TRUE(engine_->Run("testWindowController"));

    bool signalled = false;
    context.AddNativeFunction(
        "Signal", CREATE_NATIVE_ENTRY([&](Dart_NativeArguments args) {
          isolate_ = std::make_unique<flutter::Isolate>();
          signalled = true;
        }));
    while (!signalled) {
      engine_->task_runner()->ProcessTasks();
    }
  }

  void TearDown() override { engine_->Stop(); }

  int64_t engine_id() { return reinterpret_cast<int64_t>(engine_.get()); }
  flutter::Isolate* isolate() { return isolate_.get(); }
  WindowCreationRequest* creation_request() { return &creation_request_; }

 private:
  std::unique_ptr<FlutterWindowsEngine> engine_;
  std::unique_ptr<flutter::Isolate> isolate_;
  WindowCreationRequest creation_request_{
      .width = 800,
      .height = 600,
      .min_width = 0,
      .min_height = 0,
      .max_width = 0,
      .max_height = 0,
  };

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterHostWindowControllerTest);
};

}  // namespace

TEST_F(FlutterHostWindowControllerTest, WindowingInitialize) {
  IsolateScope isolate_scope(*isolate());

  static bool received_message = false;
  WindowingInitRequest init_request{
      .on_message = [](WindowsMessage* message) { received_message = true; }};

  const int64_t view_id =
      flutter_create_regular_window(engine_id(), creation_request());
  flutter_windowing_initialize(engine_id(), &init_request);
  DestroyWindow(flutter_get_window_handle(engine_id(), view_id));

  EXPECT_TRUE(received_message);
}

TEST_F(FlutterHostWindowControllerTest, HasTopLevelWindows) {
  IsolateScope isolate_scope(*isolate());

  bool has_top_level_windows =
      flutter_windowing_has_top_level_windows(engine_id());
  EXPECT_FALSE(has_top_level_windows);

  flutter_create_regular_window(engine_id(), creation_request());
  has_top_level_windows = flutter_windowing_has_top_level_windows(engine_id());
  EXPECT_TRUE(has_top_level_windows);
}

TEST_F(FlutterHostWindowControllerTest, CreateRegularWindow) {
  IsolateScope isolate_scope(*isolate());

  const int64_t view_id =
      flutter_create_regular_window(engine_id(), creation_request());
  EXPECT_EQ(view_id, 0);
}

TEST_F(FlutterHostWindowControllerTest, GetWindowHandle) {
  IsolateScope isolate_scope(*isolate());

  const int64_t view_id =
      flutter_create_regular_window(engine_id(), creation_request());
  const HWND window_handle = flutter_get_window_handle(engine_id(), view_id);
  EXPECT_NE(window_handle, nullptr);
}

TEST_F(FlutterHostWindowControllerTest, GetWindowSize) {
  IsolateScope isolate_scope(*isolate());

  const int64_t view_id =
      flutter_create_regular_window(engine_id(), creation_request());
  const HWND window_handle = flutter_get_window_handle(engine_id(), view_id);

  Size size;
  flutter_get_window_size(window_handle, &size);

  EXPECT_EQ(size.width(), creation_request()->width);
  EXPECT_EQ(size.height(), creation_request()->height);
}

TEST_F(FlutterHostWindowControllerTest, GetWindowState) {
  IsolateScope isolate_scope(*isolate());

  const int64_t view_id =
      flutter_create_regular_window(engine_id(), creation_request());
  const HWND window_handle = flutter_get_window_handle(engine_id(), view_id);
  const int64_t window_state = flutter_get_window_state(window_handle);
  EXPECT_EQ(window_state, static_cast<int64_t>(WindowState::kRestored));
}

TEST_F(FlutterHostWindowControllerTest, SetWindowState) {
  IsolateScope isolate_scope(*isolate());

  const int64_t view_id =
      flutter_create_regular_window(engine_id(), creation_request());
  const HWND window_handle = flutter_get_window_handle(engine_id(), view_id);

  const std::array kWindowStates = {
      static_cast<int64_t>(WindowState::kRestored),
      static_cast<int64_t>(WindowState::kMaximized),
      static_cast<int64_t>(WindowState::kMinimized),
  };

  for (const auto requested_state : kWindowStates) {
    flutter_set_window_state(window_handle, requested_state);
    const int64_t actual_state = flutter_get_window_state(window_handle);
    EXPECT_EQ(actual_state, requested_state);
  }
}

TEST_F(FlutterHostWindowControllerTest, SetWindowSize) {
  IsolateScope isolate_scope(*isolate());

  const int64_t view_id =
      flutter_create_regular_window(engine_id(), creation_request());
  const HWND window_handle = flutter_get_window_handle(engine_id(), view_id);

  const Size requested_size{640, 480};
  flutter_set_window_size(window_handle, requested_size.width(),
                          requested_size.height());

  Size actual_size;
  flutter_get_window_size(window_handle, &actual_size);
  EXPECT_EQ(actual_size.width(), requested_size.width());
  EXPECT_EQ(actual_size.height(), requested_size.height());
}

}  // namespace testing
}  // namespace flutter
