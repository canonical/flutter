// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/fl_settings_handler.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/embedder/test_utils/proc_table_replacement.h"
#include "flutter/shell/platform/linux/fl_binary_messenger_private.h"
#include "flutter/shell/platform/linux/fl_engine_private.h"
#include "flutter/shell/platform/linux/testing/fl_mock_binary_messenger.h"
#include "flutter/shell/platform/linux/testing/mock_settings.h"
#include "flutter/testing/testing.h"

#include "flutter/shell/platform/linux/testing/linux_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

class FlSettingsHandlerTest : public flutter::testing::LinuxTest {
 protected:
  void SetUp() override {
    messenger = fl_mock_binary_messenger_new();
    g_clear_object(&engine);
    engine =
        fl_engine_new_with_binary_messenger(FL_BINARY_MESSENGER(messenger));
    handler = fl_settings_handler_new(engine);
  }

  ~FlSettingsHandlerTest() {
    fl_binary_messenger_shutdown(FL_BINARY_MESSENGER(messenger));
    g_clear_object(&handler);
    g_clear_object(&messenger);
  }

  FlMockBinaryMessenger* messenger = nullptr;
  FlSettingsHandler* handler = nullptr;
  ::testing::NiceMock<flutter::testing::MockSettings> settings;
};

// MOCK_ENGINE_PROC is leaky by design
// NOLINTBEGIN(clang-analyzer-core.StackAddressEscape)
TEST_F(FlSettingsHandlerTest, AlwaysUse24HourFormat) {
  StartEngine();

  std::vector<FlutterUserSettings> calls;
  fl_engine_get_embedder_api(engine)->SetUserSettings = MOCK_ENGINE_PROC(
      SetUserSettings,
      ([&calls](auto engine, const FlutterUserSettings* settings) {
        calls.push_back(*settings);
        return kSuccess;
      }));

  EXPECT_CALL(settings, fl_settings_get_clock_format(
                            ::testing::Eq<FlSettings*>(settings)))
      .WillOnce(::testing::Return(FL_CLOCK_FORMAT_12H))
      .WillOnce(::testing::Return(FL_CLOCK_FORMAT_24H));

  fl_settings_handler_start(handler, settings);
  ASSERT_THAT(calls, ::testing::SizeIs(1));
  EXPECT_EQ(calls.back().struct_size, sizeof(FlutterUserSettings));
  EXPECT_FALSE(calls.back().always_use_24_hour_format);

  fl_settings_emit_changed(settings);
  ASSERT_THAT(calls, ::testing::SizeIs(2));
  EXPECT_TRUE(calls.back().always_use_24_hour_format);
}

TEST_F(FlSettingsHandlerTest, PlatformBrightness) {
  StartEngine();

  std::vector<FlutterUserSettings> calls;
  fl_engine_get_embedder_api(engine)->SetUserSettings = MOCK_ENGINE_PROC(
      SetUserSettings,
      ([&calls](auto engine, const FlutterUserSettings* settings) {
        calls.push_back(*settings);
        return kSuccess;
      }));

  EXPECT_CALL(settings, fl_settings_get_color_scheme(
                            ::testing::Eq<FlSettings*>(settings)))
      .WillOnce(::testing::Return(FL_COLOR_SCHEME_LIGHT))
      .WillOnce(::testing::Return(FL_COLOR_SCHEME_DARK));

  fl_settings_handler_start(handler, settings);
  ASSERT_THAT(calls, ::testing::SizeIs(1));
  EXPECT_EQ(calls.back().platform_brightness, kFlutterBrightnessLight);

  fl_settings_emit_changed(settings);
  ASSERT_THAT(calls, ::testing::SizeIs(2));
  EXPECT_EQ(calls.back().platform_brightness, kFlutterBrightnessDark);
}

TEST_F(FlSettingsHandlerTest, TextScaleFactor) {
  StartEngine();

  std::vector<FlutterUserSettings> calls;
  fl_engine_get_embedder_api(engine)->SetUserSettings = MOCK_ENGINE_PROC(
      SetUserSettings,
      ([&calls](auto engine, const FlutterUserSettings* settings) {
        calls.push_back(*settings);
        return kSuccess;
      }));

  EXPECT_CALL(settings, fl_settings_get_text_scaling_factor(
                            ::testing::Eq<FlSettings*>(settings)))
      .WillOnce(::testing::Return(1.0))
      .WillOnce(::testing::Return(2.0));

  fl_settings_handler_start(handler, settings);
  ASSERT_THAT(calls, ::testing::SizeIs(1));
  EXPECT_EQ(calls.back().text_scale_factor, 1.0);

  fl_settings_emit_changed(settings);
  ASSERT_THAT(calls, ::testing::SizeIs(2));
  EXPECT_EQ(calls.back().text_scale_factor, 2.0);
}

TEST_F(FlSettingsHandlerTest, DoesNotSendOnSettingsChannel) {
  StartEngine();

  gboolean called = FALSE;
  fl_mock_binary_messenger_set_json_message_channel(
      messenger, "flutter/settings",
      [](FlMockBinaryMessenger* messenger, GTask* task, FlValue* message,
         gpointer user_data) {
        gboolean* called = static_cast<gboolean*>(user_data);
        *called = TRUE;
        return fl_value_new_null();
      },
      &called);

  fl_settings_handler_start(handler, settings);
  fl_settings_emit_changed(settings);
  EXPECT_FALSE(called);
}

TEST_F(FlSettingsHandlerTest, AccessibilityFeatures) {
  StartEngine();

  std::vector<FlutterAccessibilityFeature> calls;
  fl_engine_get_embedder_api(engine)->UpdateAccessibilityFeatures =
      MOCK_ENGINE_PROC(
          UpdateAccessibilityFeatures,
          ([&calls](auto engine, FlutterAccessibilityFeature features) {
            calls.push_back(features);
            return kSuccess;
          }));

  EXPECT_CALL(settings, fl_settings_get_enable_animations(
                            ::testing::Eq<FlSettings*>(settings)))
      .WillOnce(::testing::Return(false))
      .WillOnce(::testing::Return(true))
      .WillOnce(::testing::Return(false))
      .WillOnce(::testing::Return(true));

  EXPECT_CALL(settings, fl_settings_get_high_contrast(
                            ::testing::Eq<FlSettings*>(settings)))
      .WillOnce(::testing::Return(true))
      .WillOnce(::testing::Return(false))
      .WillOnce(::testing::Return(false))
      .WillOnce(::testing::Return(true));

  fl_settings_handler_start(handler, settings);
  EXPECT_THAT(calls, ::testing::SizeIs(1));
  EXPECT_EQ(calls.back(), static_cast<FlutterAccessibilityFeature>(
                              kFlutterAccessibilityFeatureDisableAnimations |
                              kFlutterAccessibilityFeatureHighContrast));

  fl_settings_emit_changed(settings);
  EXPECT_THAT(calls, ::testing::SizeIs(2));
  EXPECT_EQ(calls.back(), static_cast<FlutterAccessibilityFeature>(0));

  fl_settings_emit_changed(settings);
  EXPECT_THAT(calls, ::testing::SizeIs(3));
  EXPECT_EQ(calls.back(), static_cast<FlutterAccessibilityFeature>(
                              kFlutterAccessibilityFeatureDisableAnimations));

  fl_settings_emit_changed(settings);
  EXPECT_THAT(calls, ::testing::SizeIs(4));
  EXPECT_EQ(calls.back(), static_cast<FlutterAccessibilityFeature>(
                              kFlutterAccessibilityFeatureHighContrast));
}
// NOLINTEND(clang-analyzer-core.StackAddressEscape)
