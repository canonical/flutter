// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/desktop_settings.h"

#include <algorithm>

namespace flutter {

namespace {

constexpr char kPortalName[] = "org.freedesktop.portal.Desktop";
constexpr char kPortalPath[] = "/org/freedesktop/portal/desktop";
constexpr char kSettingsInterface[] = "org.freedesktop.portal.Settings";

constexpr char kAppearance[] = "org.freedesktop.appearance";
constexpr char kGnomeInterface[] = "org.gnome.desktop.interface";
constexpr char kGnomeA11yInterface[] = "org.gnome.desktop.a11y.interface";

constexpr uint32_t kColorSchemeDark = 1;
constexpr uint32_t kContrastHigh = 1;
constexpr uint32_t kReducedMotionReduce = 1;

constexpr char kDarkThemeSuffix[] = "-dark";

bool EndsWith(const std::string& value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}

// Reads a value of type |T| with |read| if |signature| matches, otherwise
// skips it.
template <typename T>
bool ReadValue(DBusReader& reader,
               const std::string& signature,
               std::string_view expected,
               bool (DBusReader::*read)(T*),
               T* value) {
  if (signature != expected) {
    return reader.Skip();
  }
  return (reader.*read)(value);
}

}  // namespace

bool DesktopSettings::State::Read(const std::string& ns,
                                  const std::string& key,
                                  DBusReader& reader) {
  // The deprecated Read method wraps values in a second variant.
  std::string signature;
  size_t depth = 0;
  do {
    if (!reader.OpenVariant(&signature)) {
      return false;
    }
    depth++;
  } while (signature == "v");

  bool ok;
  std::string string_value;
  if (ns == kAppearance && key == "color-scheme") {
    ok = ReadValue(reader, signature, "u", &DBusReader::ReadUInt32,
                   &color_scheme);
  } else if (ns == kAppearance && key == "contrast") {
    ok = ReadValue(reader, signature, "u", &DBusReader::ReadUInt32, &contrast);
  } else if (ns == kAppearance && key == "reduced-motion") {
    ok = ReadValue(reader, signature, "u", &DBusReader::ReadUInt32,
                   &reduced_motion);
  } else if (ns == kGnomeInterface && key == "gtk-theme") {
    ok = ReadValue(reader, signature, "s", &DBusReader::ReadString,
                   &string_value);
    if (ok && signature == "s") {
      dark_theme = EndsWith(string_value, kDarkThemeSuffix);
    }
  } else if (ns == kGnomeInterface && key == "enable-animations") {
    ok = ReadValue(reader, signature, "b", &DBusReader::ReadBoolean,
                   &enable_animations);
  } else if (ns == kGnomeInterface && key == "text-scaling-factor") {
    double factor = text_scaling_factor;
    ok = ReadValue(reader, signature, "d", &DBusReader::ReadDouble, &factor);
    if (ok && factor > 0) {
      text_scaling_factor = factor;
    }
  } else if (ns == kGnomeInterface && key == "clock-format") {
    ok = ReadValue(reader, signature, "s", &DBusReader::ReadString,
                   &string_value);
    if (ok && signature == "s") {
      use_24_hour_clock = string_value != "12h";
    }
  } else if (ns == kGnomeA11yInterface && key == "high-contrast") {
    ok = ReadValue(reader, signature, "b", &DBusReader::ReadBoolean,
                   &gnome_high_contrast);
  } else {
    ok = reader.Skip();
  }

  for (size_t i = 0; ok && i < depth; i++) {
    ok = reader.CloseVariant();
  }
  return ok;
}

DesktopSettings::DesktopSettings(EventLoop* loop) : loop_(loop) {}

DesktopSettings::~DesktopSettings() = default;

const std::vector<std::string>& DesktopSettings::Namespaces() {
  static const std::vector<std::string> namespaces = {
      kAppearance, kGnomeInterface, kGnomeA11yInterface};
  return namespaces;
}

void DesktopSettings::Start() {
  session_ =
      DBusConnection::Connect(loop_, DBusConnection::SessionBusAddress());
  if (!session_) {
    return;
  }
  session_->set_ready_callback([this]() { OnReady(); });
  session_->set_signal_handler(
      [this](const DBusMessage& signal) { OnSignal(signal); });
}

bool DesktopSettings::dark() const {
  if (state_.color_scheme != 0) {
    return state_.color_scheme == kColorSchemeDark;
  }
  return state_.dark_theme;
}

bool DesktopSettings::high_contrast() const {
  return state_.contrast == kContrastHigh || state_.gnome_high_contrast;
}

bool DesktopSettings::reduce_motion() const {
  return state_.reduced_motion == kReducedMotionReduce ||
         !state_.enable_animations;
}

double DesktopSettings::text_scaling_factor() const {
  return state_.text_scaling_factor;
}

void DesktopSettings::AddObserver(Observer* observer) {
  observers_.push_back(observer);
}

void DesktopSettings::RemoveObserver(Observer* observer) {
  observers_.erase(std::remove(observers_.begin(), observers_.end(), observer),
                   observers_.end());
}

void DesktopSettings::OnReady() {
  session_->AddMatch(std::string("type='signal',sender='") + kPortalName +
                     "',path='" + kPortalPath + "',interface='" +
                     kSettingsInterface + "',member='SettingChanged'");

  DBusMessage call = DBusMessage::MethodCall(kPortalName, kPortalPath,
                                             kSettingsInterface, "ReadAll");
  call.writer().OpenArray("s");
  for (const std::string& ns : Namespaces()) {
    call.writer().WriteString(ns);
  }
  call.writer().CloseArray();
  session_->Call(call, [this, alive = std::weak_ptr<bool>(alive_)](
                           const DBusMessage* reply) {
    if (alive.expired() || reply == nullptr ||
        reply->type() != DBusMessage::Type::kMethodReturn) {
      return;
    }
    DBusReader reader = reply->reader();
    ReadAll(reader);
  });
}

void DesktopSettings::ReadAll(DBusReader& reader) {
  State old_state = state_;
  if (!reader.OpenArray()) {
    return;
  }
  while (reader.HasMore()) {
    std::string ns;
    if (!reader.OpenDictEntry() || !reader.ReadString(&ns) ||
        !reader.OpenArray()) {
      break;
    }
    while (reader.HasMore()) {
      std::string key;
      if (!reader.OpenDictEntry() || !reader.ReadString(&key) ||
          !state_.Read(ns, key, reader) || !reader.CloseDictEntry()) {
        break;
      }
    }
    if (!reader.ok() || !reader.CloseArray() || !reader.CloseDictEntry()) {
      break;
    }
  }
  NotifyIfChanged(old_state);
}

void DesktopSettings::OnSignal(const DBusMessage& signal) {
  if (signal.path() != kPortalPath ||
      signal.interface() != kSettingsInterface ||
      signal.member() != "SettingChanged") {
    return;
  }
  State old_state = state_;
  DBusReader reader = signal.reader();
  std::string ns;
  std::string key;
  if (reader.ReadString(&ns) && reader.ReadString(&key)) {
    state_.Read(ns, key, reader);
  }
  NotifyIfChanged(old_state);
}

void DesktopSettings::NotifyIfChanged(const State& old_state) {
  if (state_ == old_state) {
    return;
  }
  // Copied as observers may remove themselves.
  std::vector<Observer*> observers = observers_;
  for (Observer* observer : observers) {
    observer->OnDesktopSettingsChanged();
  }
}

}  // namespace flutter
