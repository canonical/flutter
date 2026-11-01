// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/display.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/wayland/wayland_display.h"
#include "flutter/shell/platform/linux/x11/x11_display.h"

namespace flutter {

std::unique_ptr<Display> Display::Create(EventLoop* event_loop) {
  const char* backend = getenv("FLUTTER_LINUX_BACKEND");
  if (backend != nullptr && backend[0] != '\0') {
    std::string_view name(backend);
    if (name == "wayland") {
      return WaylandDisplay::Create(event_loop);
    }
    if (name == "x11") {
      return X11Display::Create(event_loop);
    }
    FML_LOG(WARNING) << "Unknown FLUTTER_LINUX_BACKEND \"" << name
                     << "\", expected \"wayland\" or \"x11\"";
  }

  std::unique_ptr<Display> display = WaylandDisplay::Create(event_loop);
  if (display != nullptr) {
    return display;
  }
  if (getenv("DISPLAY") != nullptr) {
    return X11Display::Create(event_loop);
  }
  return nullptr;
}

void Display::AddOutputObserver(OutputObserver* observer) {
  output_observers_.push_back(observer);
}

void Display::RemoveOutputObserver(OutputObserver* observer) {
  output_observers_.erase(
      std::remove(output_observers_.begin(), output_observers_.end(), observer),
      output_observers_.end());
}

void Display::NotifyOutputsChanged() {
  // Observers may be removed while being notified.
  std::vector<OutputObserver*> observers = output_observers_;
  for (OutputObserver* observer : observers) {
    if (std::find(output_observers_.begin(), output_observers_.end(),
                  observer) != output_observers_.end()) {
      observer->OnOutputsChanged();
    }
  }
}

void Display::NotifyError() {
  if (error_callback_) {
    error_callback_();
  }
}

}  // namespace flutter
