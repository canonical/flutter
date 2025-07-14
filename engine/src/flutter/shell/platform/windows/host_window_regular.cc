// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/host_window_regular.h"

#include "flutter/shell/platform/windows/flutter_windows_engine.h"

namespace {
flutter::BoxConstraints GetBoxConstraints(
    const flutter::WindowSizing& content_size) {
  std::optional<flutter::Size> smallest = std::nullopt;
  std::optional<flutter::Size> biggest = std::nullopt;

  if (content_size.has_view_constraints) {
    smallest = flutter::Size(content_size.view_min_width,
                             content_size.view_min_height);
    if (content_size.view_max_width > 0 && content_size.view_max_height > 0) {
      biggest = flutter::Size(content_size.view_max_width,
                              content_size.view_max_height);
    }
  }

  return flutter::BoxConstraints(smallest, biggest);
}
}  // namespace

namespace flutter {
HostWindowRegular::HostWindowRegular(WindowManager* window_manager,
                                     FlutterWindowsEngine* engine,
                                     const WindowSizing& content_size)

    : HostWindow(
          window_manager,
          engine,
          WindowArchetype::kRegular,
          WS_OVERLAPPEDWINDOW,
          0,
          GetBoxConstraints(content_size),
          [&]() -> Rect {
            auto const constraints = GetBoxConstraints(content_size);
            std::optional<Size> const window_size = GetWindowSizeForClientSize(
                *engine->windows_proc_table(),
                Size(content_size.preferred_view_width,
                     content_size.preferred_view_height),
                constraints.smallest(), constraints.biggest(),
                WS_OVERLAPPEDWINDOW, 0, nullptr);
            return {{CW_USEDEFAULT, CW_USEDEFAULT},
                    window_size ? *window_size
                                : Size{CW_USEDEFAULT, CW_USEDEFAULT}};
          }(),
          nullptr) {
  // TODO(knopp): What about windows sized to content?
  FML_CHECK(content_size.has_preferred_view_size);
}
}  // namespace flutter