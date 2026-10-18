// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/mouse_cursor_plugin.h"

#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_method_codec.h"
#include "flutter/shell/platform/linux/fake_platform_messenger.h"
#include "flutter/shell/platform/linux/wayland_seat.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

constexpr char kChannel[] = "flutter/mousecursor";

// Sends activateSystemCursor for |kind|, returning the response.
std::vector<uint8_t> ActivateCursor(FakePlatformMessenger& messenger,
                                    EncodableValue args) {
  MethodCall<EncodableValue> call("activateSystemCursor",
                                  std::make_unique<EncodableValue>(args));
  return messenger.Receive(
      kChannel, *StandardMethodCodec::GetInstance().EncodeMethodCall(call));
}

std::vector<uint8_t> ActivateCursor(FakePlatformMessenger& messenger,
                                    const std::string& kind) {
  return ActivateCursor(messenger,
                        EncodableValue(EncodableMap{
                            {EncodableValue("device"), EncodableValue(1)},
                            {EncodableValue("kind"), EncodableValue(kind)},
                        }));
}

}  // namespace

TEST(MouseCursorPluginTest, ActivateSystemCursor) {
  FakePlatformMessenger messenger;
  std::vector<uint32_t> shapes;
  MouseCursorPlugin plugin(
      &messenger, [&shapes](uint32_t shape) { shapes.push_back(shape); });
  EXPECT_EQ(plugin.cursor(),
            static_cast<uint32_t>(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT));

  std::vector<uint8_t> response = ActivateCursor(messenger, "click");
  EXPECT_EQ(response,
            *StandardMethodCodec::GetInstance().EncodeSuccessEnvelope());
  ActivateCursor(messenger, "text");
  ActivateCursor(messenger, "resizeUpLeftDownRight");
  ActivateCursor(messenger, "none");
  EXPECT_EQ(shapes,
            (std::vector<uint32_t>{WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
                                   WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT,
                                   WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE,
                                   kHiddenCursorShape}));
  EXPECT_EQ(plugin.cursor(), kHiddenCursorShape);
}

TEST(MouseCursorPluginTest, UnknownKindIsDefault) {
  FakePlatformMessenger messenger;
  std::vector<uint32_t> shapes;
  MouseCursorPlugin plugin(
      &messenger, [&shapes](uint32_t shape) { shapes.push_back(shape); });
  ActivateCursor(messenger, "click");
  ActivateCursor(messenger, "disappearing");
  EXPECT_EQ(shapes,
            (std::vector<uint32_t>{WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
                                   WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT}));
}

TEST(MouseCursorPluginTest, UnchangedCursorNotSet) {
  FakePlatformMessenger messenger;
  int count = 0;
  MouseCursorPlugin plugin(&messenger, [&count](uint32_t shape) { count++; });
  ActivateCursor(messenger, "basic");
  ActivateCursor(messenger, "text");
  ActivateCursor(messenger, "text");
  EXPECT_EQ(count, 1);
}

TEST(MouseCursorPluginTest, BadArguments) {
  FakePlatformMessenger messenger;
  MouseCursorPlugin plugin(&messenger, [](uint32_t shape) {});
  std::vector<uint8_t> response = ActivateCursor(messenger, EncodableValue(1));
  std::unique_ptr<std::vector<uint8_t>> expected =
      StandardMethodCodec::GetInstance().EncodeErrorEnvelope(
          "Bad Arguments", "Expected arguments map");
  EXPECT_EQ(response, *expected);
}

TEST(MouseCursorPluginTest, UnknownMethod) {
  FakePlatformMessenger messenger;
  MouseCursorPlugin plugin(&messenger, [](uint32_t shape) {});
  MethodCall<EncodableValue> call("unknown", nullptr);
  std::vector<uint8_t> response = messenger.Receive(
      kChannel, *StandardMethodCodec::GetInstance().EncodeMethodCall(call));
  EXPECT_TRUE(response.empty());
}

}  // namespace testing
}  // namespace flutter
