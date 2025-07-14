// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_HOST_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_HOST_WINDOW_H_

#include <windows.h>
#include <memory>
#include <optional>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/common/geometry.h"
#include "flutter/shell/platform/common/windowing.h"
#include "flutter/shell/platform/windows/window_manager.h"

namespace flutter {

class WindowManager;
class WindowsProcTable;
class FlutterWindowsView;
class FlutterWindowsViewController;

// A Win32 window that hosts a |FlutterWindow| in its client area.
// The default implementation is a regular window, but it can be
// subclassed to create different types of windows, such as dialogs.
// See |HostWindowDialog| for an example.
class HostWindow {
 public:
  virtual ~HostWindow();

  // Creates a native Win32 window with a child view confined to its client
  // area. |window_manager| is a pointer to the window manager that manages the
  // |HostWindow|. |engine| is a pointer to the engine that manages
  // the window manager. On success, a valid window handle can be retrieved
  // via |HostWindow::GetWindowHandle|. |nullptr| will be returned
  // on failure.
  static std::unique_ptr<HostWindow> CreateRegularWindow(
      WindowManager* window_manager,
      FlutterWindowsEngine* engine,
      const WindowSizing& content_size);

  // Creates a dialog window. |window_manager| is a pointer to the window
  // manager that manages the window. |engine| is a pointer to the engine that
  // manages the window manager. |content_size| is the requested content size
  // and constraints. |owner_window| is the handle to the owner window. If
  // nullptr, the dialog is created as modeless; otherwise it is create as modal
  // to |owner_window|.
  static std::unique_ptr<HostWindow> CreateDialogWindow(
      WindowManager* window_manager,
      FlutterWindowsEngine* engine,
      const WindowSizing& content_size,
      HWND owner_window);

  // Returns the instance pointer for |hwnd| or nullptr if invalid.
  static HostWindow* GetThisFromHandle(HWND hwnd);

  // Returns the backing window handle, or nullptr if the native window is not
  // created or has already been destroyed.
  HWND GetWindowHandle() const;

  // Resizes the window to accommodate a client area of the given
  // |size|.
  void SetContentSize(const WindowSizing& size);

  // Returns the owner window, or nullptr if none.
  HostWindow* GetOwnerWindow() const;

  // Processes modal state update for single layer of window hierarchy.
  void UpdateModalStateLayer();

 protected:
  friend WindowManager;

  HostWindow(WindowManager* window_manager,
             FlutterWindowsEngine* engine,
             WindowArchetype archetype,
             DWORD window_style,
             DWORD extended_window_style,
             const BoxConstraints& box_constraints,
             Rect const initial_window_rect,
             HWND owner_window);

  // Calculates the required window size, in physical coordinates, to
  // accommodate the given |client_size|, in logical coordinates, constrained by
  // optional |smallest| and |biggest|, for a window with the specified
  // |window_style| and |extended_window_style|. If |owner_hwnd| is not null,
  // the DPI of the display with the largest area of intersection with
  // |owner_hwnd| is used for the calculation; otherwise, the primary display's
  // DPI is used. The resulting size includes window borders, non-client areas,
  // and drop shadows. On error, returns std::nullopt and logs an error message.
  static std::optional<Size> GetWindowSizeForClientSize(
      WindowsProcTable const& win32,
      Size const& client_size,
      std::optional<Size> smallest,
      std::optional<Size> biggest,
      DWORD window_style,
      DWORD extended_window_style,
      HWND owner_hwnd);

  // Processes and routes salient window messages for mouse handling,
  // size change and DPI. Delegates handling of these to member overloads that
  // inheriting classes can handle.
  virtual LRESULT HandleMessage(HWND hwnd,
                                UINT message,
                                WPARAM wparam,
                                LPARAM lparam);

  // Sets the focus to the child view window of |window|.
  static void FocusRootViewOf(HostWindow* window);

  // OS callback called by message pump. Handles the WM_NCCREATE message which
  // is passed when the non-client area is being created and enables automatic
  // non-client DPI scaling so that the non-client area automatically
  // responds to changes in DPI. Delegates other messages to the controller.
  static LRESULT WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

  // Enables or disables this window and all its descendants.
  void EnableWindowAndDescendants(bool enable);

  // Returns the first enabled descendant window. If the current window itself
  // is enabled, returns the current window.
  HostWindow* FindFirstEnabledDescendant() const;

  // Returns windows owned by this window.
  std::vector<HostWindow*> GetOwnedWindows() const;

  // Disables the window and all its descendants.
  void DisableRecursively();

  // Controller for this window.
  WindowManager* const window_manager_ = nullptr;

  // The Flutter engine that owns this window.
  FlutterWindowsEngine* engine_;

  // Controller for the view hosted in this window. Value-initialized if the
  // window is created from an existing top-level native window created by the
  // runner.
  std::unique_ptr<FlutterWindowsViewController> view_controller_;

  // The window archetype.
  WindowArchetype archetype_ = WindowArchetype::kRegular;

  // Backing handle for this window.
  HWND window_handle_ = nullptr;

  // The constraints on the window's client area.
  BoxConstraints box_constraints_;

  // True while handling WM_DESTROY; used to detect in-progress destruction.
  bool is_being_destroyed_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(HostWindow);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_HOST_WINDOW_H_
