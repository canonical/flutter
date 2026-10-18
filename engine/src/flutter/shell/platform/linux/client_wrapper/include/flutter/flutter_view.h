// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_VIEW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_VIEW_H_

#include <flutter_linux_abi.h>

#include <cstdint>

namespace flutter {

// A view of an engine, as seen by a plugin. This doesn't own the view.
class FlutterView {
 public:
  FlutterView(int64_t id, FlutterLinuxView* view) : id_(id), view_(view) {}

  FlutterView(FlutterView const&) = delete;
  FlutterView& operator=(FlutterView const&) = delete;

  // The ID Flutter uses for this view (FlutterView.viewId in Dart).
  int64_t id() const { return id_; }

  // The Wayland surface showing the view's content. Its origin is the origin
  // of the content, and it is the root of the window's surface tree, so it
  // identifies the window (e.g. to export a handle with xdg-foreign).
  wl_surface* surface() const {
    return flutter_linux_view_get_wl_surface(view_);
  }

  // The logical size of the view's content.
  int width() const {
    int width = 0;
    flutter_linux_view_get_size(view_, &width, nullptr);
    return width;
  }
  int height() const {
    int height = 0;
    flutter_linux_view_get_size(view_, nullptr, &height);
    return height;
  }

  // The number of physical pixels per logical pixel.
  double scale() const { return flutter_linux_view_get_scale(view_); }

 private:
  int64_t id_;
  FlutterLinuxView* view_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_VIEW_H_
