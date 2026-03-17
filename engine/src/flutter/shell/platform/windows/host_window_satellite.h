// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_HOST_WINDOW_SATELLITE_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_HOST_WINDOW_SATELLITE_H_

#include <cstdint>

#include "host_window.h"
#include "shell/platform/windows/flutter_windows_view.h"
#include "shell/platform/windows/window_manager.h"

namespace flutter {

// A satellite window is a popup window that follows its parent's movement.
// Unlike a tooltip, it is activatable and only runs the position callback
// once (at creation time). It cannot be minimized.
class HostWindowSatellite : public HostWindow,
                            private FlutterWindowsViewSizingDelegate {
 public:
  HostWindowSatellite(WindowManager* window_manager,
                      FlutterWindowsEngine* engine,
                      const BoxConstraints& constraints,
                      bool is_sized_to_content,
                      GetWindowPositionCallback get_position_callback,
                      HWND parent);

  // Called by |WindowManager| when the parent window moves.
  // Shifts this satellite by the same delta.
  void OnParentMoved();

  // Changes the parent of this satellite window to |new_parent|.
  // This does NOT reposition the window.
  void SetSatelliteParent(HWND new_parent);

  // Returns the current parent HWND.
  HWND GetParentHwnd() const { return parent_; }

 protected:
  LRESULT HandleMessage(HWND hwnd,
                        UINT message,
                        WPARAM wparam,
                        LPARAM lparam) override;

 private:
  // FlutterWindowsViewSizingDelegate overrides.
  void DidUpdateViewSize(int32_t width, int32_t height) override;
  WindowRect GetWorkArea() const override;

  // Runs the position callback to determine the initial window position.
  void ApplyInitialPosition();

  GetWindowPositionCallback get_position_callback_;
  HWND parent_;
  Isolate isolate_;

  // Used to track whether the view is still alive in tasks scheduled from the
  // raster thread.
  std::shared_ptr<int> view_alive_;

  // The last known screen position of the parent window (top-left corner).
  POINT last_parent_pos_ = {0, 0};

  // The current width and height of the satellite content.
  int width_ = 0;
  int height_ = 0;

  // Whether the initial position has been applied.
  bool initial_position_applied_ = false;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_HOST_WINDOW_SATELLITE_H_