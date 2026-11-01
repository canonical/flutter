// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/public/flutter_linux.h"

#include <stdlib.h>

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
      had_value_ = true;
      old_value_ = old_value;
    }
    setenv(name, value, 1);
  }

  ~ScopedEnvironmentVariable() {
    if (had_value_) {
      setenv(name_.c_str(), old_value_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

 private:
  std::string name_;
  bool had_value_ = false;
  std::string old_value_;
};

}  // namespace

TEST(FlutterLinuxTest, CreateApplicationWithoutDisplayServerFails) {
  ScopedEnvironmentVariable backend("FLUTTER_LINUX_BACKEND", "");
  ScopedEnvironmentVariable wayland_display(
      "WAYLAND_DISPLAY", "flutter-linux-test-no-such-display");
  ScopedEnvironmentVariable x11_display("DISPLAY", ":flutter-linux-test");

  std::shared_ptr<FlutterDesktopApplication> application =
      FlutterDesktopApplication::create();
  EXPECT_EQ(application, nullptr);
}

TEST(FlutterLinuxTest, CreateApplicationWithoutX11ServerFails) {
  ScopedEnvironmentVariable backend("FLUTTER_LINUX_BACKEND", "x11");
  ScopedEnvironmentVariable x11_display("DISPLAY", ":flutter-linux-test");

  std::shared_ptr<FlutterDesktopApplication> application =
      FlutterDesktopApplication::create();
  EXPECT_EQ(application, nullptr);
}

TEST(FlutterLinuxTest, CreateApplicationWithoutKmsDeviceFails) {
  ScopedEnvironmentVariable backend("FLUTTER_LINUX_BACKEND", "kms");
  ScopedEnvironmentVariable device("FLUTTER_KMS_DEVICE",
                                   "/dev/dri/flutter-linux-test");

  std::shared_ptr<FlutterDesktopApplication> application =
      FlutterDesktopApplication::create();
  EXPECT_EQ(application, nullptr);
}

}  // namespace testing
}  // namespace flutter
