// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_FLUTTER_HOST_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_FLUTTER_HOST_WINDOW_H_

#include <windows.h>
#include <memory>
#include <optional>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/common/geometry.h"
#include "flutter/shell/platform/common/windowing.h"
#include "flutter/shell/platform/windows/flutter_host_window_controller.h"

namespace flutter {

class FlutterHostWindowController;
class FlutterWindowsView;
class FlutterWindowsViewController;

// A Win32 window that hosts a |FlutterWindow| in its client area.
class FlutterHostWindow {
 public:
  virtual ~FlutterHostWindow();

  // Creates a regular window. |controller| is a pointer to the controller that
  // manages the window. |content_size| is the requested content size and
  // constraints.
  static std::unique_ptr<FlutterHostWindow> createRegular(
      FlutterHostWindowController* controller,
      FlutterWindowSizing const& content_size);

  // Creates a dialog window. |controller| is a pointer to the controller that
  // manages the window. |content_size| is the requested
  // content size and constraints. |owner_window| is the handle to the owner
  // window. If nullptr, the dialog is created as modeless; otherwise it is
  // create as modal to |owner_window|.
  static std::unique_ptr<FlutterHostWindow> createDialog(
      FlutterHostWindowController* controller,
      FlutterWindowSizing const& content_size,
      HWND owner_window);

  // Returns the instance pointer for |hwnd| or nullptr if invalid.
  static FlutterHostWindow* GetThisFromHandle(HWND hwnd);

  // Returns the backing window handle, or nullptr if not yet created.
  HWND GetWindowHandle() const;

  // Resizes the window to accommodate a client area of the given
  // |size|.
  void SetContentSize(const FlutterWindowSizing& size);

 private:
  friend FlutterHostWindowController;

  // Creates a native top-level Win32 window with a child root view confined to
  // its client area. |controller| is a pointer to the controller that manages
  // the |FlutterHostWindow|. |archetype| specifies the window type.
  // |content_size| defines the requested content size and constraints.
  // |owner_window| is the handle to owner window. Must be nullptr if
  // |archetype| is |WindowArchetype::kRegular|. For |WindowArchetype::kDialog|,
  // the dialog is modeless if |owner_window| is nullptr; otherwise, it is
  // modal to |owner_window|. On success, a valid window handle can be retrieved
  // via |FlutterHostWindow::GetWindowHandle|.
  FlutterHostWindow(FlutterHostWindowController* controller,
                    WindowArchetype archetype,
                    const FlutterWindowSizing& content_size,
                    HWND owner_window);

  // Sets the focus to the root view window of |window|.
  static void FocusRootViewOf(FlutterHostWindow* window);

  // OS callback called by message pump. Handles the WM_NCCREATE message which
  // is passed when the non-client area is being created and enables automatic
  // non-client DPI scaling so that the non-client area automatically
  // responds to changes in DPI. Delegates other messages to the controller.
  static LRESULT WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

  // Enables or disables this window and all its descendants.
  void EnableWindowAndDescendants(bool enable);

  // Returns the first enabled descendant window. If the current window itself
  // is enabled, returns the current window.
  FlutterHostWindow* FindFirstEnabledDescendant() const;

  // Returns windows owned by this window.
  std::vector<FlutterHostWindow*> GetOwnedWindows() const;

  // Returns the owner window, or nullptr if none.
  FlutterHostWindow* GetOwnerWindow() const;

  // Processes and routes salient window messages for mouse handling,
  // size change and DPI. Delegates handling of these to member overloads that
  // inheriting classes can handle.
  LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

  // Inserts |content| into the window tree.
  void SetChildContent(HWND content);

  // Enforces modal behavior. This favors enabling most recently created
  // modal window higest up in the window hierarchy.
  void UpdateModalState();

  // Disables the window and all its descendants.
  void DisableRecursively();

  // Processes modal state update for single layer of window hierarchy.
  void UpdateModalStateLayer();

  // Controller for this window.
  FlutterHostWindowController* const window_controller_ = nullptr;

  // Controller for the root view. Value-initialized if the window is created
  // from an existing top-level native window created by the runner.
  std::unique_ptr<FlutterWindowsViewController> view_controller_;

  // The window archetype.
  WindowArchetype archetype_ = WindowArchetype::kRegular;

  // Backing handle for this window.
  HWND window_handle_ = nullptr;

  // Backing handle for the hosted root view window.
  HWND child_content_ = nullptr;

  // The minimum size of the window's client area, if defined.
  std::optional<Size> min_size_;

  // The maximum size of the window's client area, if defined.
  std::optional<Size> max_size_;

  // True while handling WM_DESTROY; used to detect in-progress destruction.
  bool is_being_destroyed_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterHostWindow);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_FLUTTER_HOST_WINDOW_H_
