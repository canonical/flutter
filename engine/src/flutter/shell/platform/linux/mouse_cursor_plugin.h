// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_MOUSE_CURSOR_PLUGIN_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_MOUSE_CURSOR_PLUGIN_H_

#include <cstdint>
#include <functional>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/platform_messenger.h"

namespace flutter {

// Implements the flutter/mousecursor channel, which sets the cursor shown
// over an engine's views.
class MouseCursorPlugin {
 public:
  // Called with the new cursor, a wp_cursor_shape_device_v1 shape or
  // kHiddenCursorShape.
  using CursorCallback = std::function<void(uint32_t shape)>;

  // |messenger| must outlive the plugin.
  MouseCursorPlugin(PlatformMessenger* messenger, CursorCallback on_cursor);
  ~MouseCursorPlugin();

  // The cursor the framework has requested.
  uint32_t cursor() const { return cursor_; }

 private:
  // Handles a method call from the framework, returning the encoded
  // response.
  std::vector<uint8_t> HandleMethodCall(const uint8_t* data, size_t size);

  PlatformMessenger* messenger_;
  CursorCallback on_cursor_;
  uint32_t cursor_;

  FML_DISALLOW_COPY_AND_ASSIGN(MouseCursorPlugin);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_MOUSE_CURSOR_PLUGIN_H_
