// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WINDOW_H_

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/decoration_renderer.h"
#include "flutter/shell/platform/linux/protocols/wayland-client-protocol.h"
#include "flutter/shell/platform/linux/wayland_display.h"
#include "flutter/shell/platform/linux/window_decoration.h"

namespace flutter {

// A window on a Wayland compositor: either a top-level window, or a popup
// placed relative to a parent window.
//
// The window is configured on the platform thread, but contents are presented
// from the raster thread. The platform thread only commits the surface to
// update the decorations, synchronized with presentation by a lock.
//
// If |decoration_renderer| is provided top-level windows draw their own title
// bar and shadow in a subsurface below the content. Decorations of a new size
// are shown with the first content buffer of the matching size, so resizing
// is atomic and presenting content doesn't redraw the decorations.
class WaylandWindow : public WindowDecoration::Delegate {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when the window size or scale changes.
    virtual void OnWindowConfigure() = 0;

    // Called when the user requests the window be closed, or when the
    // compositor dismisses a popup.
    virtual void OnWindowClose() = 0;

    // Called when the window is activated or deactivated, maximized,
    // made fullscreen, or a popup is moved.
    virtual void OnWindowStateChanged() {}

    // Called when a frame has been shown on the screen.
    // |presentation_time_ns| is when it was shown, on CLOCK_MONOTONIC.
    // |refresh_ns| is the time until the next display refresh, or zero if
    // that is unknown.
    virtual void OnWindowPresented(uint64_t presentation_time_ns,
                                   uint64_t refresh_ns) = 0;

    // Called when the compositor indicates it is a good time to draw a new
    // frame, after a previously presented frame has been handled.
    virtual void OnWindowFrameDone() = 0;
  };

  enum class Role {
    // A top-level window, optionally attached to a parent window.
    kToplevel,
    // A window placed relative to its parent, such as a menu or tooltip.
    // Popups are undecorated and are dismissed by the compositor when the
    // user interacts with another application.
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
    // compositor may move the popup to keep it on screen.
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
    // placed relative to. Required for popups.
    WaylandWindow* parent = nullptr;

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

  // Creates a window. This blocks until the compositor has configured the
  // window, without dispatching events for other windows. Decorations are
  // drawn with |decoration_renderer| if it is not nullptr. Returns nullptr on
  // failure.
  static std::unique_ptr<WaylandWindow> Create(
      WaylandDisplay* display,
      DecorationRenderer* decoration_renderer,
      Delegate* delegate,
      const Params& params);

  ~WaylandWindow() override;

  // The logical size of the window content, excluding decorations.
  int width() const { return width_; }
  int height() const { return height_; }

  // The surface showing the window content.
  wl_surface* surface() const { return surface_; }

  // The number of physical pixels per logical pixel.
  double scale() const { return scale_; }

  // The size of the window content in physical pixels.
  int physical_width() const;
  int physical_height() const;

  Role role() const { return role_; }
  WaylandWindow* parent() const { return parent_; }

  // Top-level window state.
  bool maximized() const { return maximized_; }
  bool fullscreen() const { return fullscreen_; }
  bool activated() const { return activated_; }

  // The position of a popup's content relative to its parent's content, in
  // logical pixels.
  int popup_x() const;
  int popup_y() const;

  // The largest content size the compositor suggests for a top-level window,
  // in logical pixels, or zero if unknown.
  int max_suggested_width() const;
  int max_suggested_height() const;

  void SetTitle(const std::string& title);
  void SetApplicationId(const std::string& application_id);

  // Changes the content size of a top-level window when it is not
  // maximized, fullscreen or tiled.
  void SetSize(int width, int height);

  // Changes the content size to match content that has been drawn at
  // |width| x |height| logical pixels, ignoring the size limits. Used for
  // windows whose size is chosen by their content. The delegate's
  // OnWindowConfigure is not called.
  void ResizeToContent(int width, int height);

  // Limits the content size of a top-level window, resizing it if it is
  // outside the limits.
  void SetSizeLimits(const SizeLimits& limits);

  void SetMaximized(bool maximized);
  void SetMinimized();
  void SetFullscreen(bool fullscreen);

  // Asks the compositor to focus a top-level window. Compositors may refuse
  // if the request is not in response to user input.
  void Activate();

  // Attaches a top-level window to |parent|, or detaches it if nullptr.
  void SetParent(WaylandWindow* parent);

  // Moves a popup. Has no effect if the compositor doesn't support it.
  void SetPositioner(const Positioner& positioner);

  // Stops |Present| waiting for the compositor, so it doesn't block a thread
  // that is removing the window.
  void StopWaiting();

  // Timeline points used to explicitly synchronize access to a buffer with
  // the compositor.
  struct SyncPoints {
    wp_linux_drm_syncobj_timeline_v1* timeline;

    // The point the compositor waits for before reading the buffer.
    uint64_t acquire_point;

    // The point the compositor signals when it has finished with the buffer.
    uint64_t release_point;
  };

  // Shows |buffer| in the window. The buffer is |width| x |height| pixels
  // and rendered with OpenGL conventions (bottom row first).
  //
  // If |sync| is provided, the compositor uses it to synchronize access to
  // the buffer instead of implicit synchronization. Once a buffer has been
  // presented with |sync|, all following buffers must also provide it.
  //
  // Each frame is shown for at least one display refresh if the compositor
  // supports wp_fifo_v1, rather than being replaced by the next frame.
  //
  // The delegate is informed when the compositor is ready for another frame,
  // and if the compositor supports wp_presentation, when the frame is shown.
  //
  // If a popup changes size, it is repositioned first and this blocks until
  // the compositor has placed it, so it moves and resizes in the same frame.
  //
  // May be called from any thread, but only from one thread at a time.
  void Present(wl_buffer* buffer,
               int width,
               int height,
               const SyncPoints* sync);

  // The number of presented frames the compositor hasn't yet indicated it
  // is ready to follow with another frame. These don't complete while the
  // window is hidden.
  size_t frames_in_flight();

  // |WindowDecoration::Delegate|
  void OnDecorationFrame(const DecorationFrame& frame) override;
  void OnDecorationClose() override;
  bool IsDecorationBufferPending(RenderBuffer* buffer) override;

 private:
  WaylandWindow(WaylandDisplay* display, Delegate* delegate);

  bool Initialize(DecorationRenderer* decoration_renderer,
                  const Params& params);

  // The window geometry origin relative to the content surface.
  int geometry_x() const;
  int geometry_y() const;

  // The height of the title bar above the content, if shown.
  int title_bar_height() const;

  // Places a popup of |width| x |height| logical pixels and waits for the
  // compositor to configure it. Called with |surface_mutex_| held in |lock|.
  void RepositionForSize(std::unique_lock<std::mutex>& lock,
                         int width,
                         int height);

  // Creates an xdg_positioner placing a popup of the given logical size.
  xdg_positioner* CreatePositioner(const Positioner& positioner,
                                   int width,
                                   int height);

  // Sends the size limits to the compositor.
  void UpdateSizeLimits();

  // Applies |limits_| to the content size.
  void ClampSize(int* width, int* height) const;

  // Changes the content size and informs the delegate if it changed.
  void Resize(int width, int height);

  // Redraws the decorations if the window state has changed.
  void UpdateDecoration();

  // Applies |frame| and sets the window geometry. The changes are shown on
  // the next commit of the surface. Called with |surface_mutex_| held.
  void ApplyDecorationFrame(const DecorationFrame& frame);

  static void SurfaceEnter(void* data, wl_surface* surface, wl_output* output);
  static void SurfaceLeave(void* data, wl_surface* surface, wl_output* output);
  static void XdgSurfaceConfigure(void* data,
                                  xdg_surface* xdg_surface,
                                  uint32_t serial);
  static void ToplevelConfigure(void* data,
                                xdg_toplevel* toplevel,
                                int32_t width,
                                int32_t height,
                                wl_array* states);
  static void ToplevelClose(void* data, xdg_toplevel* toplevel);
  static void ToplevelConfigureBounds(void* data,
                                      xdg_toplevel* toplevel,
                                      int32_t width,
                                      int32_t height);
  static void ToplevelWmCapabilities(void* data,
                                     xdg_toplevel* toplevel,
                                     wl_array* capabilities);
  static void PopupConfigure(void* data,
                             xdg_popup* popup,
                             int32_t x,
                             int32_t y,
                             int32_t width,
                             int32_t height);
  static void PopupDone(void* data, xdg_popup* popup);
  static void PopupRepositioned(void* data, xdg_popup* popup, uint32_t token);
  static void ActivationTokenDone(void* data,
                                  xdg_activation_token_v1* token,
                                  const char* token_string);
  static void FeedbackSyncOutput(void* data,
                                 struct wp_presentation_feedback* feedback,
                                 wl_output* output);
  static void FeedbackPresented(void* data,
                                struct wp_presentation_feedback* feedback,
                                uint32_t tv_sec_hi,
                                uint32_t tv_sec_lo,
                                uint32_t tv_nsec,
                                uint32_t refresh,
                                uint32_t seq_hi,
                                uint32_t seq_lo,
                                uint32_t flags);
  static void FeedbackDiscarded(void* data,
                                struct wp_presentation_feedback* feedback);
  static void FrameDone(void* data, wl_callback* callback, uint32_t time);
  static void PreferredScale(void* data,
                             wp_fractional_scale_v1* fractional_scale,
                             uint32_t scale);

  static const wl_surface_listener kSurfaceListener;
  static const xdg_surface_listener kXdgSurfaceListener;
  static const xdg_toplevel_listener kToplevelListener;
  static const xdg_popup_listener kPopupListener;
  static const xdg_activation_token_v1_listener kActivationTokenListener;
  static const wp_fractional_scale_v1_listener kFractionalScaleListener;
  static const wp_presentation_feedback_listener kFeedbackListener;
  static const wl_callback_listener kFrameListener;

  // Destroys |feedback| once it has delivered its result.
  void RemoveFeedback(struct wp_presentation_feedback* feedback);

  WaylandDisplay* display_;
  Delegate* delegate_;
  Role role_ = Role::kToplevel;
  WaylandWindow* parent_ = nullptr;

  wl_surface* surface_ = nullptr;
  xdg_surface* xdg_surface_ = nullptr;
  xdg_toplevel* toplevel_ = nullptr;
  xdg_popup* popup_ = nullptr;
  xdg_dialog_v1* dialog_ = nullptr;
  xdg_activation_token_v1* activation_token_ = nullptr;
  wp_viewport* viewport_ = nullptr;
  wp_fractional_scale_v1* fractional_scale_ = nullptr;
  wp_fifo_v1* fifo_ = nullptr;

  std::unique_ptr<WindowDecoration> decoration_;

  // Held while changing and committing the surfaces.
  std::mutex surface_mutex_;

  // Decorations waiting for content of the size they were drawn for.
  std::optional<DecorationFrame> pending_decoration_;

  // The size of the last content buffer presented, in physical pixels.
  int presented_width_ = 0;
  int presented_height_ = 0;

  // True once a commit has set a fifo barrier. Only accessed by the thread
  // presenting.
  bool fifo_barrier_set_ = false;

  // Created on the first present with explicit synchronization.
  wp_linux_drm_syncobj_surface_v1* syncobj_surface_ = nullptr;

  bool configured_ = false;

  // State from the current configure sequence, applied when it completes.
  int pending_width_ = 0;
  int pending_height_ = 0;
  bool pending_maximized_ = false;
  bool pending_fullscreen_ = false;
  bool pending_tiled_ = false;
  bool pending_activated_ = false;
  int pending_popup_x_ = 0;
  int pending_popup_y_ = 0;

  bool maximized_ = false;
  bool fullscreen_ = false;
  bool tiled_ = false;
  bool activated_ = false;
  bool can_minimize_ = true;
  bool can_maximize_ = true;
  bool resizable_ = true;
  SizeLimits limits_;
  std::string title_;

  // The size the compositor suggests for a top-level window's geometry, or
  // zero if unknown.
  int bounds_width_ = 0;
  int bounds_height_ = 0;

  // A popup's position relative to its parent's window geometry, and how it
  // was last placed.
  int popup_geometry_x_ = 0;
  int popup_geometry_y_ = 0;

  // How a popup was last placed, guarded by |surface_mutex_| as popups are
  // repositioned when presenting.
  Positioner positioner_;
  int positioned_width_ = 0;
  int positioned_height_ = 0;
  uint32_t reposition_token_ = 0;

  // The last reposition the compositor responded to, and the last one whose
  // configure has been acknowledged. Guarded by |surface_mutex_|.
  uint32_t received_reposition_token_ = 0;
  uint32_t acked_reposition_token_ = 0;

  // The reposition |Present| is waiting for, if any, and whether it should
  // stop waiting. Guarded by |surface_mutex_|.
  std::optional<uint32_t> awaited_reposition_token_;
  bool stop_waiting_ = false;
  std::condition_variable reposition_condition_;

  // The content size to return to when the window stops being maximized,
  // fullscreen or tiled.
  int floating_width_ = 0;
  int floating_height_ = 0;

  std::atomic<int> width_ = 0;
  std::atomic<int> height_ = 0;
  std::atomic<double> scale_ = 1.0;

  // Feedback requested on the raster thread, delivered on the platform
  // thread.
  std::mutex feedback_mutex_;
  std::set<struct wp_presentation_feedback*> pending_feedback_;

  // Frame callbacks requested on the raster thread, delivered on the
  // platform thread.
  std::mutex frame_mutex_;
  std::set<wl_callback*> pending_frames_;

  FML_DISALLOW_COPY_AND_ASSIGN(WaylandWindow);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WINDOW_H_
