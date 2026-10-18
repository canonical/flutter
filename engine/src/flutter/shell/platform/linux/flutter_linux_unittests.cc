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

TEST(FlutterLinuxTest, CreateApplicationWithoutCompositorFails) {
  ScopedEnvironmentVariable display("WAYLAND_DISPLAY",
                                    "flutter-linux-test-no-such-display");

  std::shared_ptr<FlutterDesktopApplication> application =
      FlutterDesktopApplication::create();
  EXPECT_EQ(application, nullptr);
}

}  // namespace testing
}  // namespace flutter
