// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/desktop_settings.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <string>

#include "flutter/shell/platform/linux/fake_dbus_bus.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

constexpr char kAppearance[] = "org.freedesktop.appearance";
constexpr char kGnomeInterface[] = "org.gnome.desktop.interface";
constexpr char kGnomeA11yInterface[] = "org.gnome.desktop.a11y.interface";

// Reads a setting whose value is written to a variant by |write|.
bool ReadSetting(DesktopSettings::State* state,
                 const std::string& ns,
                 const std::string& key,
                 const std::string& signature,
                 const std::function<void(DBusWriter&)>& write) {
  DBusWriter writer;
  writer.OpenVariant(signature);
  write(writer);
  writer.CloseVariant();
  // A value after the setting checks the variant is fully read.
  writer.WriteByte(42);
  DBusReader reader(writer.data().data(), writer.data().size(),
                    writer.signature(), false);
  if (!state->Read(ns, key, reader)) {
    return false;
  }
  uint8_t byte = 0;
  return reader.ReadByte(&byte) && byte == 42;
}

class ScopedEnv {
 public:
  ScopedEnv(const char* name, const std::string& value) : name_(name) {
    const char* old = getenv(name);
    if (old != nullptr) {
      old_value_ = old;
    }
    setenv(name, value.c_str(), 1);
  }
  ~ScopedEnv() {
    if (old_value_.has_value()) {
      setenv(name_, old_value_->c_str(), 1);
    } else {
      unsetenv(name_);
    }
  }

 private:
  const char* name_;
  std::optional<std::string> old_value_;
};

class CountingObserver : public DesktopSettings::Observer {
 public:
  void OnDesktopSettingsChanged() override { count++; }
  int count = 0;
};

}  // namespace

TEST(DesktopSettingsTest, Defaults) {
  EventLoop loop;
  DesktopSettings settings(&loop);
  EXPECT_FALSE(settings.dark());
  EXPECT_FALSE(settings.high_contrast());
  EXPECT_FALSE(settings.reduce_motion());
  EXPECT_EQ(settings.text_scaling_factor(), 1.0);
  EXPECT_TRUE(settings.use_24_hour_clock());
}

TEST(DesktopSettingsTest, ReadsColorScheme) {
  DesktopSettings::State state;
  EXPECT_TRUE(ReadSetting(&state, kAppearance, "color-scheme", "u",
                          [](DBusWriter& w) { w.WriteUInt32(1); }));
  EXPECT_EQ(state.color_scheme, 1u);
}

TEST(DesktopSettingsTest, ReadsGnomeSettings) {
  DesktopSettings::State state;
  EXPECT_TRUE(
      ReadSetting(&state, kGnomeInterface, "gtk-theme", "s",
                  [](DBusWriter& w) { w.WriteString("Adwaita-dark"); }));
  EXPECT_TRUE(state.dark_theme);
  EXPECT_TRUE(ReadSetting(&state, kGnomeInterface, "text-scaling-factor", "d",
                          [](DBusWriter& w) { w.WriteDouble(1.5); }));
  EXPECT_EQ(state.text_scaling_factor, 1.5);
  EXPECT_TRUE(ReadSetting(&state, kGnomeInterface, "clock-format", "s",
                          [](DBusWriter& w) { w.WriteString("12h"); }));
  EXPECT_FALSE(state.use_24_hour_clock);
  EXPECT_TRUE(ReadSetting(&state, kGnomeInterface, "enable-animations", "b",
                          [](DBusWriter& w) { w.WriteBoolean(false); }));
  EXPECT_FALSE(state.enable_animations);
  EXPECT_TRUE(ReadSetting(&state, kGnomeA11yInterface, "high-contrast", "b",
                          [](DBusWriter& w) { w.WriteBoolean(true); }));
  EXPECT_TRUE(state.gnome_high_contrast);
}

TEST(DesktopSettingsTest, ReadsNestedVariant) {
  DesktopSettings::State state;
  EXPECT_TRUE(
      ReadSetting(&state, kAppearance, "contrast", "v", [](DBusWriter& w) {
        w.OpenVariant("u");
        w.WriteUInt32(1);
        w.CloseVariant();
      }));
  EXPECT_EQ(state.contrast, 1u);
}

TEST(DesktopSettingsTest, SkipsUnknownAndMistypedSettings) {
  DesktopSettings::State state;
  EXPECT_TRUE(ReadSetting(&state, kAppearance, "accent-color", "(ddd)",
                          [](DBusWriter& w) {
                            w.OpenStruct();
                            w.WriteDouble(1);
                            w.WriteDouble(0);
                            w.WriteDouble(0);
                            w.CloseStruct();
                          }));
  EXPECT_TRUE(ReadSetting(&state, kAppearance, "color-scheme", "s",
                          [](DBusWriter& w) { w.WriteString("dark"); }));
  EXPECT_EQ(state, DesktopSettings::State());
}

TEST(DesktopSettingsTest, IgnoresInvalidScaleFactor) {
  DesktopSettings::State state;
  EXPECT_TRUE(ReadSetting(&state, kGnomeInterface, "text-scaling-factor", "d",
                          [](DBusWriter& w) { w.WriteDouble(0); }));
  EXPECT_EQ(state.text_scaling_factor, 1.0);
}

class DesktopSettingsBusTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string name = "flutter-settings-test-" + std::to_string(getpid());
    listen_fd_.reset(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    ASSERT_TRUE(listen_fd_.is_valid());
    sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path + 1, name.data(), name.size());
    socklen_t length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                              1 + name.size());
    ASSERT_EQ(
        bind(listen_fd_.get(), reinterpret_cast<sockaddr*>(&address), length),
        0);
    ASSERT_EQ(listen(listen_fd_.get(), 1), 0);
    env_.emplace("DBUS_SESSION_BUS_ADDRESS", "unix:abstract=" + name);

    settings_ = std::make_unique<DesktopSettings>(&loop_);
    settings_->AddObserver(&observer_);
    settings_->Start();

    fml::UniqueFD bus_fd(
        accept4(listen_fd_.get(), nullptr, nullptr, SOCK_CLOEXEC));
    ASSERT_TRUE(bus_fd.is_valid());
    bus_ = std::make_unique<FakeBus>(std::move(bus_fd));
    bus_->Start();
  }

  void TearDown() override { settings_->RemoveObserver(&observer_); }

  // Reads messages until the call to |member|.
  std::optional<DBusMessage> ReadCall(const std::string& member) {
    while (true) {
      if (!RunUntil(loop_, [this]() { return bus_->HasInput(); })) {
        return std::nullopt;
      }
      std::optional<DBusMessage> message = bus_->ReadMessage();
      if (!message.has_value() || message->member() == member) {
        return message;
      }
    }
  }

  EventLoop loop_;
  fml::UniqueFD listen_fd_;
  std::optional<ScopedEnv> env_;
  CountingObserver observer_;
  std::unique_ptr<DesktopSettings> settings_;
  std::unique_ptr<FakeBus> bus_;
};

TEST_F(DesktopSettingsBusTest, ReadsAllAndWatchesChanges) {
  std::optional<DBusMessage> call = ReadCall("ReadAll");
  ASSERT_TRUE(call.has_value());
  EXPECT_EQ(call->destination(), "org.freedesktop.portal.Desktop");
  EXPECT_EQ(call->path(), "/org/freedesktop/portal/desktop");
  EXPECT_EQ(call->interface(), "org.freedesktop.portal.Settings");
  EXPECT_EQ(call->signature(), "as");

  DBusMessage reply = DBusMessage::MethodReturn(*call);
  DBusWriter& writer = reply.writer();
  writer.OpenArray("{sa{sv}}");
  writer.OpenDictEntry();
  writer.WriteString(kAppearance);
  writer.OpenArray("{sv}");
  writer.OpenDictEntry();
  writer.WriteString("color-scheme");
  writer.OpenVariant("u");
  writer.WriteUInt32(1);
  writer.CloseVariant();
  writer.CloseDictEntry();
  writer.CloseArray();
  writer.CloseDictEntry();
  writer.OpenDictEntry();
  writer.WriteString(kGnomeInterface);
  writer.OpenArray("{sv}");
  writer.OpenDictEntry();
  writer.WriteString("text-scaling-factor");
  writer.OpenVariant("d");
  writer.WriteDouble(1.25);
  writer.CloseVariant();
  writer.CloseDictEntry();
  writer.CloseArray();
  writer.CloseDictEntry();
  writer.CloseArray();
  bus_->Send(reply);

  ASSERT_TRUE(RunUntil(loop_, [this]() { return observer_.count == 1; }));
  EXPECT_TRUE(settings_->dark());
  EXPECT_EQ(settings_->text_scaling_factor(), 1.25);

  DBusMessage signal =
      DBusMessage::Signal("/org/freedesktop/portal/desktop",
                          "org.freedesktop.portal.Settings", "SettingChanged");
  signal.writer().WriteString(kAppearance);
  signal.writer().WriteString("color-scheme");
  signal.writer().OpenVariant("u");
  signal.writer().WriteUInt32(2);
  signal.writer().CloseVariant();
  bus_->Send(signal);

  ASSERT_TRUE(RunUntil(loop_, [this]() { return observer_.count == 2; }));
  EXPECT_FALSE(settings_->dark());
}

}  // namespace testing
}  // namespace flutter
