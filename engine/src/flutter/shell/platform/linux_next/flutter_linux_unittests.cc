// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_next/public/flutter_linux.h"

#include "gtest/gtest.h"

namespace flutter {
namespace testing {

TEST(FlutterLinuxTest, ApplicationQuitBeforeRun) {
  std::shared_ptr<FlutterDesktopApplication> application =
      FlutterDesktopApplication::create();
  ASSERT_NE(application, nullptr);

  application->quit();
  application->run();
}

TEST(FlutterLinuxTest, EngineRunFailsWithoutAssets) {
  std::shared_ptr<FlutterDesktopApplication> application =
      FlutterDesktopApplication::create();
  ASSERT_NE(application, nullptr);

  FlutterDesktopEngineProperties properties;
  properties.assets_path = "/flutter-linux-test/no-such-assets";
  properties.icu_data_path = "/flutter-linux-test/no-such-icudtl.dat";
  properties.aot_library_path = "/flutter-linux-test/no-such-libapp.so";
  std::shared_ptr<FlutterDesktopEngine> engine =
      FlutterDesktopEngine::create(application, properties);
  ASSERT_NE(engine, nullptr);
  EXPECT_EQ(engine->application(), application);

  EXPECT_FALSE(engine->is_running());
  EXPECT_FALSE(engine->run());
  EXPECT_FALSE(engine->is_running());
}

}  // namespace testing
}  // namespace flutter
