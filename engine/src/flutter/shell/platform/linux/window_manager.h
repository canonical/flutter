// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WINDOW_MANAGER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WINDOW_MANAGER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/common/public/flutter_export.h"
#include "flutter/shell/platform/common/windowing.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/flutter_linux_view.h"

namespace flutter {

class FlutterLinuxEngine;

// Events sent to the framework about a window.
enum class WindowEvent : int32_t {
  // The window size or state changed, or the window moved.
  kChanged = 0,
  // The user requested the window be closed.
  kCloseRequested = 1,
  // The window was destroyed.
  kDestroyed = 2,
};

// Receives window events. Called on the platform thread.
typedef void (*WindowEventCallback)(int64_t view_id, int32_t event);

struct WindowingInitRequest {
  WindowEventCallback on_event;
};

// Limits on the size of a window's content in logical pixels. Maximums may
// be infinite.
struct WindowConstraints {
  bool has_constraints;
  double min_width;
  double min_height;
  double max_width;
  double max_height;
};

// Where a popup or tooltip is placed relative to its parent.
struct WindowPositionerRequest {
  // The rectangle in the parent's content the window is placed relative to,
  // in logical pixels.
  double anchor_x;
  double anchor_y;
  double anchor_width;
  double anchor_height;

  // WindowPositionerAnchor values: the point on the anchor rectangle the
  // window is placed at, and the point on the window placed there.
  int32_t parent_anchor;
  int32_t child_anchor;

  // Moves the window from the anchor point, in logical pixels.
  double offset_x;
  double offset_y;

  // How the window may be moved to keep it on screen, a combination of
  // WindowPositionerConstraintAdjustment bits.
  uint32_t constraint_adjustment;
};

struct WindowCreationRequest {
  // A WindowArchetype.
  int32_t archetype;

  // The view of the parent window, or -1 for none.
  int64_t parent_view_id;

  // The initial content size in logical pixels. If |has_size| is false, the
  // window is sized to fit its content.
  bool has_size;
  double width;
  double height;

  WindowConstraints constraints;

  // The UTF-8 title, or nullptr.
  const char* title;

  bool resizable;

  // Where popups and tooltips are placed.
  WindowPositionerRequest positioner;
};

struct WindowState {
  // The content size in logical pixels.
  double width;
  double height;

  bool activated;
  bool maximized;
  bool fullscreen;

  // The position of a popup or tooltip's content relative to its parent's
  // content, in logical pixels.
  double offset_x;
  double offset_y;
};

// Creates and manages the windows the framework requests through the
// windowing API. Each window has its own view.
//
// Used on the platform thread.
class WindowManager {
 public:
  explicit WindowManager(FlutterLinuxEngine* engine);

  // Destroys the remaining windows without informing the framework.
  ~WindowManager();

  void Initialize(const WindowingInitRequest& request);

  // Creates a window and returns its view ID, or -1 on failure.
  FlutterViewId CreateWindow(const WindowCreationRequest& request);

  // Destroys a window and the windows attached to it.
  void DestroyWindow(FlutterViewId view_id);

  // Returns the view of a window, or nullptr if there is no such window.
  FlutterLinuxView* GetView(FlutterViewId view_id) const;

  bool GetWindowState(FlutterViewId view_id, WindowState* state) const;
  void SetSize(FlutterViewId view_id, double width, double height);
  void SetConstraints(FlutterViewId view_id,
                      const WindowConstraints& constraints);
  void SetTitle(FlutterViewId view_id, const char* title);
  void SetMaximized(FlutterViewId view_id, bool maximized);
  void SetMinimized(FlutterViewId view_id);
  void SetFullscreen(FlutterViewId view_id, bool fullscreen);
  void Activate(FlutterViewId view_id);
  void SetParent(FlutterViewId view_id, FlutterViewId parent_view_id);
  void UpdatePosition(FlutterViewId view_id,
                      const WindowPositionerRequest& positioner);

 private:
  struct Window {
    std::unique_ptr<FlutterLinuxView> view;
    WindowArchetype archetype;
    FlutterViewId parent_view_id;
    bool resizable;
  };

  Window* FindWindow(FlutterViewId view_id);
  const Window* FindWindow(FlutterViewId view_id) const;

  // Sends |event| to the framework.
  void SendEvent(FlutterViewId view_id, WindowEvent event);

  // Handles the user or compositor requesting a window be closed.
  void OnCloseRequested(FlutterViewId view_id);

  // Adds the windows attached to |view_id| to |result|, deepest first.
  void GetDescendants(FlutterViewId view_id,
                      std::vector<FlutterViewId>* result) const;

  FlutterLinuxEngine* engine_;
  WindowEventCallback on_event_ = nullptr;
  std::map<FlutterViewId, Window> windows_;

  FML_DISALLOW_COPY_AND_ASSIGN(WindowManager);
};

}  // namespace flutter

// Functions the framework calls through FFI. |engine_id| identifies the
// engine, as provided to the framework by the embedder API.
extern "C" {

FLUTTER_EXPORT void InternalFlutterLinux_WindowManager_Initialize(
    int64_t engine_id,
    const flutter::WindowingInitRequest* request);

FLUTTER_EXPORT int64_t InternalFlutterLinux_WindowManager_CreateWindow(
    int64_t engine_id,
    const flutter::WindowCreationRequest* request);

FLUTTER_EXPORT void InternalFlutterLinux_WindowManager_DestroyWindow(
    int64_t engine_id,
    int64_t view_id);

FLUTTER_EXPORT bool InternalFlutterLinux_WindowManager_GetWindowState(
    int64_t engine_id,
    int64_t view_id,
    flutter::WindowState* state);

FLUTTER_EXPORT void InternalFlutterLinux_WindowManager_SetSize(
    int64_t engine_id,
    int64_t view_id,
    double width,
    double height);

FLUTTER_EXPORT void InternalFlutterLinux_WindowManager_SetConstraints(
    int64_t engine_id,
    int64_t view_id,
    const flutter::WindowConstraints* constraints);

FLUTTER_EXPORT void InternalFlutterLinux_WindowManager_SetTitle(
    int64_t engine_id,
    int64_t view_id,
    const char* title);

FLUTTER_EXPORT void InternalFlutterLinux_WindowManager_SetMaximized(
    int64_t engine_id,
    int64_t view_id,
    bool maximized);

FLUTTER_EXPORT void InternalFlutterLinux_WindowManager_SetMinimized(
    int64_t engine_id,
    int64_t view_id);

FLUTTER_EXPORT void InternalFlutterLinux_WindowManager_SetFullscreen(
    int64_t engine_id,
    int64_t view_id,
    bool fullscreen);

FLUTTER_EXPORT void InternalFlutterLinux_WindowManager_Activate(
    int64_t engine_id,
    int64_t view_id);

FLUTTER_EXPORT void InternalFlutterLinux_WindowManager_SetParent(
    int64_t engine_id,
    int64_t view_id,
    int64_t parent_view_id);

FLUTTER_EXPORT void InternalFlutterLinux_WindowManager_UpdatePosition(
    int64_t engine_id,
    int64_t view_id,
    const flutter::WindowPositionerRequest* positioner);

}  // extern "C"

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WINDOW_MANAGER_H_
