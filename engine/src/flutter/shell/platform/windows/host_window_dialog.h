// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_HOST_WINDOW_DIALOG_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_HOST_WINDOW_DIALOG_H_

#include "host_window.h"

namespace flutter {
class HostWindowDialog : public HostWindow {
 public:
  HostWindowDialog(WindowManager* window_manager,
                   FlutterWindowsEngine* engine,
                   const WindowSizing& content_size,
                   HWND owner_window);

 protected:
  LRESULT HandleMessage(HWND hwnd,
                        UINT message,
                        WPARAM wparam,
                        LPARAM lparam) override;

 private:
  // Enforces modal behavior. This favors enabling most recently created
  // modal window higest up in the window hierarchy.
  void UpdateModalState();
};
}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_HOST_WINDOW_DIALOG_H_
