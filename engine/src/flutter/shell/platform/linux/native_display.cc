// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/native_display.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/kms/kms_display.h"
#include "flutter/shell/platform/linux/wayland/wayland_display.h"
#include "flutter/shell/platform/linux/x11/x11_display.h"

namespace flutter {

namespace {

bool IsSet(const char* name) {
  const char* value = getenv(name);
  return value != nullptr && value[0] != '\0';
}

}  // namespace

std::unique_ptr<NativeDisplay> NativeDisplay::Create(EventLoop* event_loop) {
  const char* backend = getenv("FLUTTER_LINUX_BACKEND");
  if (backend != nullptr && backend[0] != '\0') {
    std::string_view name(backend);
    if (name == "wayland") {
      return WaylandDisplay::Create(event_loop);
    }
    if (name == "x11") {
      return X11Display::Create(event_loop);
    }
    if (name == "kms") {
      return KmsDisplay::Create(event_loop);
    }
    FML_LOG(WARNING) << "Unknown FLUTTER_LINUX_BACKEND \"" << name
                     << "\", expected \"wayland\", \"x11\" or \"kms\"";
  }

  // The Wayland library connects to "wayland-0" when WAYLAND_DISPLAY isn't
  // set, which would find a compositor running on another virtual terminal.
  if (IsSet("WAYLAND_DISPLAY") || IsSet("WAYLAND_SOCKET")) {
    std::unique_ptr<NativeDisplay> display = WaylandDisplay::Create(event_loop);
    if (display != nullptr) {
      return display;
    }
  }
  if (IsSet("DISPLAY")) {
    return X11Display::Create(event_loop);
  }
  // Without a display server, show the application on the screen directly.
  return KmsDisplay::Create(event_loop);
}

void NativeDisplay::AddOutputObserver(OutputObserver* observer) {
  output_observers_.push_back(observer);
}

void NativeDisplay::RemoveOutputObserver(OutputObserver* observer) {
  output_observers_.erase(
      std::remove(output_observers_.begin(), output_observers_.end(), observer),
      output_observers_.end());
}

void NativeDisplay::NotifyOutputsChanged() {
  // Observers may be removed while being notified.
  std::vector<OutputObserver*> observers = output_observers_;
  for (OutputObserver* observer : observers) {
    if (std::find(output_observers_.begin(), output_observers_.end(),
                  observer) != output_observers_.end()) {
      observer->OnOutputsChanged();
    }
  }
}

void NativeDisplay::NotifyError() {
  if (error_callback_) {
    error_callback_();
  }
}

}  // namespace flutter
