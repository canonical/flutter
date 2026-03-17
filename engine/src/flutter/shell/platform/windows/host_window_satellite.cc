// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/host_window_satellite.h"

#include "flutter/shell/platform/windows/flutter_windows_view_controller.h"
#include "shell/platform/windows/window_manager.h"

namespace flutter {

HostWindowSatellite::HostWindowSatellite(
    WindowManager* window_manager,
    FlutterWindowsEngine* engine,
    const BoxConstraints& constraints,
    bool is_sized_to_content,
    GetWindowPositionCallback get_position_callback,
    HWND parent)
    : HostWindow(window_manager, engine),
      get_position_callback_(get_position_callback),
      parent_(parent),
      isolate_(Isolate::Current()),
      view_alive_(std::make_shared<int>(0)) {
  // Use minimum constraints as initial size to ensure the view can be created
  // with valid metrics. If is_sized_to_content is true, the size will be
  // updated when content is rendered.
  auto const initial_width =
      static_cast<double>(constraints.smallest().width());
  auto const initial_height =
      static_cast<double>(constraints.smallest().height());

  InitializeFlutterView(HostWindowInitializationParams{
      .archetype = WindowArchetype::kSatellite,
      .window_style = WS_POPUP,
      .extended_window_style = WS_EX_TOOLWINDOW,
      .box_constraints = constraints,
      .initial_window_rect = {{0, 0}, {initial_width, initial_height}},
      .title = L"",
      .owner_window = parent,
      .nCmdShow = SW_SHOWNORMAL,
      .sizing_delegate = this,
      .is_sized_to_content = is_sized_to_content});
  SetWindowLongPtr(window_handle_, GWLP_HWNDPARENT,
                   reinterpret_cast<LONG_PTR>(parent_));

  // Record the parent's current screen position for delta-based movement
  // tracking.
  RECT parent_rect;
  GetWindowRect(parent_, &parent_rect);
  last_parent_pos_ = {parent_rect.left, parent_rect.top};

  // Apply initial positioning using the callback.
  width_ = static_cast<int>(initial_width);
  height_ = static_cast<int>(initial_height);
  ApplyInitialPosition();
}

void HostWindowSatellite::ApplyInitialPosition() {
  if (!get_position_callback_) {
    return;
  }

  RECT parent_client_rect;
  GetClientRect(parent_, &parent_client_rect);

  POINT parent_top_left = {parent_client_rect.left, parent_client_rect.top};
  POINT parent_bottom_right = {parent_client_rect.right,
                               parent_client_rect.bottom};

  ClientToScreen(parent_, &parent_top_left);
  ClientToScreen(parent_, &parent_bottom_right);

  WindowRect work_area = GetWorkArea();

  IsolateScope scope(isolate_);
  auto rect = get_position_callback_(
      WindowSize{width_, height_},
      WindowRect{parent_top_left.x, parent_top_left.y,
                 parent_bottom_right.x - parent_top_left.x,
                 parent_bottom_right.y - parent_top_left.y},
      work_area);
  SetWindowPos(window_handle_, nullptr, rect->left, rect->top, rect->width,
               rect->height, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
  free(rect);

  initial_position_applied_ = true;
}

void HostWindowSatellite::DidUpdateViewSize(int32_t width, int32_t height) {
  // This is called from the raster thread.
  std::weak_ptr<int> weak_view_alive = view_alive_;
  engine_->task_runner()->PostTask([this, width, height, weak_view_alive]() {
    auto const view_alive = weak_view_alive.lock();
    if (!view_alive) {
      return;
    }
    if (width_ == width && height_ == height) {
      return;
    }
    if (is_being_destroyed_) {
      return;
    }

    width_ = width;
    height_ = height;

    // Only run the position callback for the initial placement.
    // After that, the window is positioned solely by following the parent.
    if (!initial_position_applied_) {
      ApplyInitialPosition();
    }
  });
}

WindowRect HostWindowSatellite::GetWorkArea() const {
  constexpr int32_t kDefaultWorkAreaSize = 10000;
  WindowRect work_area = {0, 0, kDefaultWorkAreaSize, kDefaultWorkAreaSize};
  HMONITOR monitor = MonitorFromWindow(parent_, MONITOR_DEFAULTTONEAREST);
  if (monitor) {
    MONITORINFO monitor_info = {0};
    monitor_info.cbSize = sizeof(monitor_info);
    if (GetMonitorInfo(monitor, &monitor_info)) {
      work_area.left = monitor_info.rcWork.left;
      work_area.top = monitor_info.rcWork.top;
      work_area.width = monitor_info.rcWork.right - monitor_info.rcWork.left;
      work_area.height = monitor_info.rcWork.bottom - monitor_info.rcWork.top;
    }
  }
  return work_area;
}

void HostWindowSatellite::OnParentMoved() {
  RECT parent_rect;
  GetWindowRect(parent_, &parent_rect);

  LONG dx = parent_rect.left - last_parent_pos_.x;
  LONG dy = parent_rect.top - last_parent_pos_.y;

  last_parent_pos_ = {parent_rect.left, parent_rect.top};

  if (dx == 0 && dy == 0) {
    return;
  }

  RECT satellite_rect;
  GetWindowRect(window_handle_, &satellite_rect);
  SetWindowPos(window_handle_, nullptr, satellite_rect.left + dx,
               satellite_rect.top + dy, 0, 0,
               SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
}

void HostWindowSatellite::SetSatelliteParent(HWND new_parent) {
  parent_ = new_parent;
  SetWindowLongPtr(window_handle_, GWLP_HWNDPARENT,
                   reinterpret_cast<LONG_PTR>(new_parent));

  // Update the tracked parent position so future deltas are relative to the
  // new parent's current position, without moving the satellite.
  RECT parent_rect;
  GetWindowRect(new_parent, &parent_rect);
  last_parent_pos_ = {parent_rect.left, parent_rect.top};
}

LRESULT HostWindowSatellite::HandleMessage(HWND hwnd,
                                           UINT message,
                                           WPARAM wparam,
                                           LPARAM lparam) {
  switch (message) {
    case WM_SYSCOMMAND:
      // Block minimize.
      if ((wparam & 0xFFF0) == SC_MINIMIZE) {
        return 0;
      }
      break;
  }

  return HostWindow::HandleMessage(hwnd, message, wparam, lparam);
}

}  // namespace flutter