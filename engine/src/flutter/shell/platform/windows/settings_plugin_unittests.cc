// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/settings_plugin.h"

#include <memory>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/test_utils/proc_table_replacement.h"
#include "flutter/shell/platform/windows/flutter_windows_engine.h"
#include "flutter/shell/platform/windows/task_runner.h"
#include "flutter/shell/platform/windows/testing/engine_modifier.h"
#include "flutter/shell/platform/windows/testing/flutter_windows_engine_builder.h"
#include "flutter/shell/platform/windows/testing/windows_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

class MockSettingsPlugin : public SettingsPlugin {
 public:
  explicit MockSettingsPlugin(FlutterWindowsEngine* engine,
                              TaskRunner* task_runner)
      : SettingsPlugin(engine, task_runner) {}

  virtual ~MockSettingsPlugin() = default;

  bool is_high_contrast() { return is_high_contrast_; }

  // |SettingsPlugin|
  MOCK_METHOD(bool, GetAlwaysUse24HourFormat, (), (override));
  MOCK_METHOD(float, GetTextScaleFactor, (), (override));
  MOCK_METHOD(PlatformBrightness, GetPreferredBrightness, (), (override));

  MOCK_METHOD(void, WatchPreferredBrightnessChanged, (), (override));
  MOCK_METHOD(void, WatchTextScaleFactorChanged, (), (override));

 private:
  FML_DISALLOW_COPY_AND_ASSIGN(MockSettingsPlugin);
};

class SettingsPluginTest : public WindowsTest {
 protected:
  void SetUp() override {
    WindowsTest::SetUp();

    FlutterWindowsEngineBuilder builder{GetContext()};
    engine_ = builder.Build();

    EngineModifier modifier{engine_.get()};
    modifier.embedder_api().SetUserSettings = MOCK_ENGINE_PROC(
        SetUserSettings, ([this](auto engine, const FlutterUserSettings* s) {
          calls_.push_back(*s);
          return kSuccess;
        }));
  }

  FlutterWindowsEngine* engine() { return engine_.get(); }
  std::vector<FlutterUserSettings>& calls() { return calls_; }

 private:
  std::unique_ptr<FlutterWindowsEngine> engine_;
  std::vector<FlutterUserSettings> calls_;
};

}  // namespace

TEST_F(SettingsPluginTest, SendSettingsSetsUserSettings) {
  ::testing::NiceMock<MockSettingsPlugin> settings_plugin(engine(), nullptr);
  ON_CALL(settings_plugin, GetAlwaysUse24HourFormat)
      .WillByDefault(::testing::Return(true));
  ON_CALL(settings_plugin, GetTextScaleFactor)
      .WillByDefault(::testing::Return(1.5));
  ON_CALL(settings_plugin, GetPreferredBrightness)
      .WillByDefault(
          ::testing::Return(SettingsPlugin::PlatformBrightness::kDark));

  settings_plugin.SendSettings();

  ASSERT_EQ(calls().size(), 1u);
  EXPECT_EQ(calls()[0].struct_size, sizeof(FlutterUserSettings));
  EXPECT_TRUE(calls()[0].always_use_24_hour_format);
  EXPECT_EQ(calls()[0].text_scale_factor, 1.5);
  EXPECT_EQ(calls()[0].platform_brightness, kFlutterBrightnessDark);
}

TEST_F(SettingsPluginTest, SendSettingsGetsSettings) {
  ::testing::NiceMock<MockSettingsPlugin> settings_plugin(engine(), nullptr);

  EXPECT_CALL(settings_plugin, GetAlwaysUse24HourFormat).Times(1);
  EXPECT_CALL(settings_plugin, GetTextScaleFactor).Times(1);
  EXPECT_CALL(settings_plugin, GetPreferredBrightness).Times(1);

  settings_plugin.SendSettings();
}

TEST_F(SettingsPluginTest, StartWatchingStartsWatchingChanges) {
  ::testing::NiceMock<MockSettingsPlugin> settings_plugin(engine(), nullptr);

  EXPECT_CALL(settings_plugin, WatchPreferredBrightnessChanged).Times(1);
  EXPECT_CALL(settings_plugin, WatchTextScaleFactorChanged).Times(1);

  settings_plugin.StartWatching();
}

TEST_F(SettingsPluginTest, HighContrastModeHonored) {
  ::testing::NiceMock<MockSettingsPlugin> settings_plugin(engine(), nullptr);

  settings_plugin.UpdateHighContrastMode(true);
  EXPECT_TRUE(settings_plugin.is_high_contrast());

  settings_plugin.UpdateHighContrastMode(false);
  EXPECT_FALSE(settings_plugin.is_high_contrast());

  EXPECT_EQ(calls().size(), 2u);
}

}  // namespace testing
}  // namespace flutter
