// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_next/public/flutter_linux.h"

#include <cstdlib>
#include <optional>
#include <string>

#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

// Sets an environment variable for the lifetime of this object.
class ScopedEnvironmentVariable {
 public:
  ScopedEnvironmentVariable(const char* name, const char* value) : name_(name) {
    const char* old_value = getenv(name);
    if (old_value != nullptr) {
      old_value_ = old_value;
    }
    setenv(name, value, 1);
  }

  ~ScopedEnvironmentVariable() {
    if (old_value_.has_value()) {
      setenv(name_.c_str(), old_value_->c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

 private:
  std::string name_;
  std::optional<std::string> old_value_;
};

}  // namespace

TEST(FlutterLinuxTest, CreateApplicationWithoutDisplayServerFails) {
  ScopedEnvironmentVariable wayland_display(
      "WAYLAND_DISPLAY", "flutter-linux-test-no-such-display");
  FlutterDesktopResult result;
  EXPECT_EQ(FlutterDesktopApplication::create(&result), nullptr);
  EXPECT_EQ(result, FlutterDesktopResult::kNoDisplay);
}

TEST(FlutterLinuxTest, ApplicationQuitBeforeRun) {
  std::shared_ptr<FlutterDesktopApplication> application =
      FlutterDesktopApplication::create();
  if (application == nullptr) {
    GTEST_SKIP() << "No display server available";
  }

  application->quit();
  application->run();
}

TEST(FlutterLinuxTest, EngineRunFailsWithoutAssets) {
  std::shared_ptr<FlutterDesktopApplication> application =
      FlutterDesktopApplication::create();
  if (application == nullptr) {
    GTEST_SKIP() << "No display server available";
  }

  FlutterDesktopEngineProperties properties;
  properties.assets_path = "/flutter-linux-test/no-such-assets";
  properties.icu_data_path = "/flutter-linux-test/no-such-icudtl.dat";
  properties.aot_library_path = "/flutter-linux-test/no-such-libapp.so";
  std::shared_ptr<FlutterDesktopEngine> engine =
      FlutterDesktopEngine::create(application, properties);
  ASSERT_NE(engine, nullptr);
  EXPECT_EQ(engine->application(), application);

  EXPECT_FALSE(engine->is_running());
  FlutterDesktopResult result;
  EXPECT_FALSE(engine->run(&result));
  EXPECT_EQ(result, FlutterDesktopResult::kFailed);
  EXPECT_FALSE(engine->is_running());
}

TEST(FlutterLinuxTest, CreateViews) {
  std::shared_ptr<FlutterDesktopApplication> application =
      FlutterDesktopApplication::create();
  if (application == nullptr) {
    GTEST_SKIP() << "No display server available";
  }

  std::shared_ptr<FlutterDesktopEngine> engine =
      FlutterDesktopEngine::create(application, {});
  ASSERT_NE(engine, nullptr);

  std::shared_ptr<FlutterDesktopView> view1 =
      FlutterDesktopView::create(engine, 100, 100);
  ASSERT_NE(view1, nullptr);
  EXPECT_EQ(view1->id(), 1);

  std::shared_ptr<FlutterDesktopView> implicit_view =
      FlutterDesktopView::create_implicit(engine, 100, 100);
  ASSERT_NE(implicit_view, nullptr);
  EXPECT_EQ(implicit_view->id(), 0);

  std::shared_ptr<FlutterDesktopView> view2 =
      FlutterDesktopView::create(engine, 100, 100);
  ASSERT_NE(view2, nullptr);
  EXPECT_EQ(view2->id(), 2);
}

TEST(FlutterLinuxTest, CreateImplicitViewOnlyOnce) {
  std::shared_ptr<FlutterDesktopApplication> application =
      FlutterDesktopApplication::create();
  if (application == nullptr) {
    GTEST_SKIP() << "No display server available";
  }

  std::shared_ptr<FlutterDesktopEngine> engine =
      FlutterDesktopEngine::create(application, {});
  ASSERT_NE(engine, nullptr);

  FlutterDesktopResult result;
  std::shared_ptr<FlutterDesktopView> view =
      FlutterDesktopView::create_implicit(engine, 100, 100, &result);
  ASSERT_NE(view, nullptr);
  EXPECT_EQ(result, FlutterDesktopResult::kSuccess);
  EXPECT_EQ(FlutterDesktopView::create_implicit(engine, 100, 100, &result),
            nullptr);
  EXPECT_EQ(result, FlutterDesktopResult::kInvalidState);

  // It can't be recreated once removed.
  view.reset();
  EXPECT_EQ(FlutterDesktopView::create_implicit(engine, 100, 100, &result),
            nullptr);
  EXPECT_EQ(result, FlutterDesktopResult::kInvalidState);
}

}  // namespace testing
}  // namespace flutter
