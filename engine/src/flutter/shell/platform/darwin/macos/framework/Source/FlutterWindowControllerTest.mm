// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "flutter/shell/platform/common/isolate_scope.h"
#import "flutter/shell/platform/darwin/macos/framework/Source/FlutterEngineTestUtils.h"
#import "flutter/shell/platform/darwin/macos/framework/Source/FlutterWindowController.h"
#import "flutter/testing/testing.h"
#import "third_party/googletest/googletest/include/gtest/gtest.h"

namespace flutter::testing {

using FlutterWindowControllerTest = FlutterEngineTest;

TEST_F(FlutterWindowControllerTest, Test1) {
  FlutterEngine* engine = GetFlutterEngine();
  FlutterWindowController* controller = [[FlutterWindowController alloc] init];
  controller.engine = engine;

  std::unique_ptr<flutter::Isolate> isolate;
  bool signalled = false;

  AddNativeCallback("SignalNativeTest", CREATE_NATIVE_ENTRY([&](Dart_NativeArguments args) {
                      fprintf(stderr, "Signal native test\n");
                      isolate = std::make_unique<flutter::Isolate>();
                      signalled = true;
                    }));

  EXPECT_TRUE([engine runWithEntrypoint:@"testWindowController"]);
  while (!signalled) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1, YES);
  }

  FlutterWindowCreationRequest request{
      .width = 800,
      .height = 600,
      .on_close = [] {},
      .on_size_change = [] {},
  };

  int64_t engine_id = reinterpret_cast<int64_t>(engine);

  IsolateScope isolate_scope(*isolate.get());
  int64_t handle = FlutterCreateRegularWindow(engine_id, &request);
  EXPECT_EQ(handle, 1);
}
}  // namespace flutter::testing
