// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DESKTOP_SETTINGS_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DESKTOP_SETTINGS_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/dbus_connection.h"
#include "flutter/shell/platform/linux/dbus_message.h"
#include "flutter/shell/platform/linux/event_loop.h"

namespace flutter {

// The user's desktop preferences, read from the XDG desktop portal's
// settings (org.freedesktop.portal.Settings). The defaults are used if the
// portal is unavailable.
class DesktopSettings {
 public:
  class Observer {
   public:
    virtual ~Observer() = default;

    virtual void OnDesktopSettingsChanged() = 0;
  };

  // The settings as stored by the desktop. Some preferences have both a
  // standard and a GNOME setting.
  struct State {
    // org.freedesktop.appearance color-scheme: 0 no preference, 1 dark,
    // 2 light.
    uint32_t color_scheme = 0;

    // org.gnome.desktop.interface gtk-theme ends with "-dark", used if there
    // is no color scheme preference.
    bool dark_theme = false;

    // org.freedesktop.appearance contrast: 0 no preference, 1 higher.
    uint32_t contrast = 0;

    // org.gnome.desktop.a11y.interface high-contrast.
    bool gnome_high_contrast = false;

    // org.freedesktop.appearance reduced-motion: 0 no preference, 1 reduce.
    uint32_t reduced_motion = 0;

    // org.gnome.desktop.interface enable-animations.
    bool enable_animations = true;

    // org.gnome.desktop.interface text-scaling-factor.
    double text_scaling_factor = 1.0;

    // org.gnome.desktop.interface clock-format is "24h".
    bool use_24_hour_clock = true;

    bool operator==(const State& other) const = default;

    // Updates the setting |key| in namespace |ns| from |reader|, positioned
    // at the setting's variant. Returns false if the value is invalid.
    // Unknown settings are skipped.
    bool Read(const std::string& ns,
              const std::string& key,
              DBusReader& reader);
  };

  explicit DesktopSettings(EventLoop* loop);
  ~DesktopSettings();

  // Connects to the session bus and reads the settings.
  void Start();

  bool dark() const;
  bool high_contrast() const;
  bool reduce_motion() const;
  double text_scaling_factor() const;
  bool use_24_hour_clock() const { return state_.use_24_hour_clock; }

  const State& state() const { return state_; }

  // Adds or removes an observer, which must be removed before it is
  // destroyed.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // The setting namespaces read from the portal.
  static const std::vector<std::string>& Namespaces();

 private:
  void OnReady();
  void OnSignal(const DBusMessage& signal);

  // Reads the a{sa{sv}} reply of ReadAll.
  void ReadAll(DBusReader& reader);

  // Notifies the observers if the settings have changed from |old_state|.
  void NotifyIfChanged(const State& old_state);

  EventLoop* loop_;
  std::unique_ptr<DBusConnection> session_;
  State state_;
  std::vector<Observer*> observers_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(DesktopSettings);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DESKTOP_SETTINGS_H_
