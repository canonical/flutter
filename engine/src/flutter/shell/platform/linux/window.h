// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WINDOW_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "flutter/shell/platform/linux/input_handler.h"
#include "flutter/shell/platform/linux/input_method.h"
#include "flutter/shell/platform/linux/platform_view_geometry.h"
// Provides the xdg_positioner values used to place popups on all display
// backends.
#include "flutter/shell/platform/linux/wayland/protocols/xdg-shell-client-protocol.h"

struct wp_linux_drm_syncobj_timeline_v1;

namespace flutter {

class LinuxPlatformView;
class RenderBuffer;

// A window showing Flutter content: either a top-level window, or a popup
// placed relative to a parent window. Implemented by each display backend.
//
// The window is configured on the platform thread, but contents are presented
// from the raster thread.
class Window {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when the window size or scale changes.
    virtual void OnWindowConfigure() = 0;

    // Called when the user requests the window be closed, or when the
    // display server dismisses a popup.
    virtual void OnWindowClose() = 0;

    // Called when the window is activated or deactivated, maximized,
    // made fullscreen, or a popup is moved.
    virtual void OnWindowStateChanged() {}

    // Called when the window title changes.
    virtual void OnWindowTitleChanged() {}

    // Called when the window moves to a different output, or the outputs
    // change.
    virtual void OnWindowDisplayChanged() {}

    // Called when a frame has been shown on the screen.
    // |presentation_time_ns| is when it was shown, on CLOCK_MONOTONIC.
    // |refresh_ns| is the time until the next display refresh, or zero if
    // that is unknown.
    virtual void OnWindowPresented(uint64_t presentation_time_ns,
                                   uint64_t refresh_ns) = 0;

    // Called when the display server indicates it is a good time to draw a
    // new frame, after a previously presented frame has been handled.
    virtual void OnWindowFrameDone() = 0;

    // Runs |task| with the render context current, on the thread |Present|
    // is called on, or immediately if rendering has stopped. Used to release
    // GPU resources the window allocated while presenting.
    virtual void RunWithRenderContext(std::function<void()> task) { task(); }
  };

  enum class Role {
    // A top-level window, optionally attached to a parent window.
    kToplevel,
    // A window placed relative to its parent, such as a menu or tooltip.
    // Popups are undecorated and are dismissed when the user interacts with
    // another application.
    kPopup,
  };

  // Where a popup is placed relative to its parent, using xdg_positioner
  // values.
  struct Positioner {
    // The rectangle in the parent the popup is placed relative to, in
    // logical pixels relative to the parent's content.
    Rect anchor_rect;
    // An xdg_positioner_anchor: the point on |anchor_rect| the popup is
    // placed at.
    uint32_t anchor = XDG_POSITIONER_ANCHOR_NONE;
    // An xdg_positioner_gravity: the direction the popup extends from the
    // anchor point.
    uint32_t gravity = XDG_POSITIONER_GRAVITY_NONE;
    // A combination of xdg_positioner_constraint_adjustment bits, how the
    // popup may be moved to keep it on screen.
    uint32_t constraint_adjustment = 0;
    // Moves the popup from the anchor point, in logical pixels.
    int offset_x = 0;
    int offset_y = 0;
  };

  // Limits on the size of the window content in logical pixels. A maximum of
  // zero is unlimited.
  struct SizeLimits {
    int min_width = 0;
    int min_height = 0;
    int max_width = 0;
    int max_height = 0;
  };

  struct Params {
    Role role = Role::kToplevel;
    // The window a top-level window is attached to, or the window a popup is
    // placed relative to. Required for popups. Must be from the same display.
    Window* parent = nullptr;
    // The initial logical size of the content.
    int width = 0;
    int height = 0;
    // For top-level windows: whether the window has decorations, blocks
    // input to its parent, and can be resized by the user.
    bool decorated = true;
    bool modal = false;
    bool resizable = true;
    SizeLimits size_limits;
    std::string title;
    std::string application_id;
    // For popups: where the popup is placed, and whether it receives input.
    // Popups that don't receive input pass it to the windows below.
    Positioner positioner;
    bool accepts_input = true;
  };

  // Timeline points used to explicitly synchronize access to a buffer with
  // the display server. Only used on Wayland.
  struct SyncPoints {
    wp_linux_drm_syncobj_timeline_v1* timeline;
    // The point the display server waits for before reading the buffer.
    uint64_t acquire_point;
    // The point the display server signals when it has finished with the
    // buffer.
    uint64_t release_point;
  };

  // A layer shown above the content, either Flutter content drawn above a
  // platform view (an overlay) or a platform view.
  struct Layer {
    // The overlay's buffer, the same size as the content, or nullptr for a
    // platform view.
    RenderBuffer* buffer = nullptr;
    // Used to synchronize the overlay's buffer, as with |sync| for the
    // content. Ignored unless |sync| is provided.
    SyncPoints sync = {};
    // The part of the overlay that has content, in physical pixels.
    PixelRect content;
    // The platform view and where it is shown.
    std::shared_ptr<LinuxPlatformView> platform_view;
    PlatformViewGeometry geometry;
  };

  virtual ~Window() = default;

  // The logical size of the window content, excluding decorations.
  virtual int width() const = 0;
  virtual int height() const = 0;

  // The number of physical pixels per logical pixel.
  virtual double scale() const = 0;

  // The ID of the output the window is shown on, or the first output if the
  // window is not shown. Zero if there are no outputs.
  virtual uint32_t display_id() const = 0;

  // The size of the window content in physical pixels.
  virtual int physical_width() const = 0;
  virtual int physical_height() const = 0;

  virtual Role role() const = 0;
  virtual Window* parent() const = 0;

  // Top-level window state.
  virtual bool maximized() const = 0;
  virtual bool fullscreen() const = 0;
  virtual bool activated() const = 0;
  // True if the display server has stopped showing the window, for example
  // when it is minimized or on another workspace.
  virtual bool suspended() const = 0;
  virtual bool modal() const = 0;
  virtual bool resizable() const = 0;
  virtual const std::string& title() const = 0;

  // The position of a popup's content relative to its parent's content, in
  // logical pixels.
  virtual int popup_x() const = 0;
  virtual int popup_y() const = 0;

  // The largest content size the display server suggests for a top-level
  // window, in logical pixels, or zero if unknown.
  virtual int max_suggested_width() const = 0;
  virtual int max_suggested_height() const = 0;

  // True if the pointer is over the window content.
  virtual bool has_pointer_focus() const = 0;

  // True if the window content has keyboard focus.
  virtual bool has_keyboard_focus() const = 0;

  // Sends input events for the window content to the handlers, or stops
  // sending them if they are nullptr.
  virtual void SetInputHandlers(InputHandler* input_handler,
                                KeyboardHandler* keyboard_handler,
                                InputMethodHandler* input_method_handler) = 0;

  virtual void SetTitle(const std::string& title) = 0;
  virtual void SetApplicationId(const std::string& application_id) = 0;

  // Changes the content size of a top-level window when it is not
  // maximized, fullscreen or tiled.
  virtual void SetSize(int width, int height) = 0;

  // Changes the content size to match content that has been drawn at
  // |width| x |height| logical pixels, ignoring the size limits. Used for
  // windows whose size is chosen by their content. The delegate's
  // OnWindowConfigure is not called.
  virtual void ResizeToContent(int width, int height) = 0;

  // Limits the content size of a top-level window, resizing it if it is
  // outside the limits.
  virtual void SetSizeLimits(const SizeLimits& limits) = 0;

  virtual void SetMaximized(bool maximized) = 0;
  virtual void SetMinimized() = 0;
  virtual void SetFullscreen(bool fullscreen) = 0;

  // Asks the display server to focus a top-level window. It may refuse if
  // the request is not in response to user input.
  virtual void Activate() = 0;

  // Attaches a top-level window to |parent|, or detaches it if nullptr.
  virtual void SetParent(Window* parent) = 0;

  // Moves a popup. Has no effect if the display server doesn't support it.
  virtual void SetPositioner(const Positioner& positioner) = 0;

  // Stops |Present| waiting for the display server, so it doesn't block a
  // thread that is removing the window.
  virtual void StopWaiting() = 0;

  // Shows |buffer| in the window. The buffer is rendered with OpenGL
  // conventions (bottom row first).
  //
  // If |sync| is provided, it is used to synchronize access to the buffer
  // instead of implicit synchronization. Once a buffer has been presented
  // with |sync|, all following buffers must also provide it.
  //
  // The delegate is informed when the display server is ready for another
  // frame, and if supported, when the frame is shown.
  //
  // |layers| are shown above the content in the order given. Overlays and
  // platform views not in |layers| are hidden.
  //
  // Called on the raster thread with the render context current.
  virtual void Present(RenderBuffer* buffer,
                       const SyncPoints* sync,
                       const std::vector<Layer>& layers = {}) = 0;

  // The radius in physical pixels to round the bottom corners of a buffer
  // of |width| x |height| pixels to, so it matches the decorations it will
  // be shown with. Zero if the corners are square.
  virtual double ContentCornerRadius(int width, int height) = 0;

  // The number of presented frames the display server hasn't yet indicated
  // it is ready to follow with another frame. These don't complete while the
  // window is hidden.
  virtual size_t frames_in_flight() = 0;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WINDOW_H_
