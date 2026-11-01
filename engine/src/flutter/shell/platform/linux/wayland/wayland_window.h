// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_WINDOW_H_

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/platform_view_geometry.h"
#include "flutter/shell/platform/linux/wayland/decoration_renderer.h"
#include "flutter/shell/platform/linux/wayland/protocols/wayland-client-protocol.h"
#include "flutter/shell/platform/linux/wayland/protocols/xdg-shell-client-protocol.h"
#include "flutter/shell/platform/linux/wayland/wayland_display.h"
#include "flutter/shell/platform/linux/wayland/wayland_platform_view.h"
#include "flutter/shell/platform/linux/wayland/window_decoration.h"
#include "flutter/shell/platform/linux/window.h"

namespace flutter {

// A window on a Wayland compositor: either a top-level window, or a popup
// placed relative to a parent window.
//
// The window is configured on the platform thread, but contents are presented
// from the raster thread. The platform thread only commits the surface to
// update the decorations, synchronized with presentation by a lock.
//
// Decorated top-level windows draw their own title bar and shadow in
// subsurfaces below the content if |decoration_renderer| is provided. They use
// server-side decorations if the compositor supports xdg-decoration and either
// requires them or the window can't draw its own. Decorations of a new size are
// shown with the first content buffer of the matching size, so resizing is
// atomic and presenting content doesn't redraw the decorations.
class WaylandWindow : public Window,
                      public WindowDecoration::Delegate,
                      public WaylandDisplay::OutputObserver {
 public:
  // Creates a window. This blocks until the compositor has configured the
  // window, without dispatching events for other windows. Decorations are
  // drawn with |decoration_renderer| if it is not nullptr. Returns nullptr on
  // failure.
  static std::unique_ptr<WaylandWindow> Create(
      WaylandDisplay* display,
      DecorationRenderer* decoration_renderer,
      Window::Delegate* delegate,
      const Params& params);

  ~WaylandWindow() override;

  // The surface showing the window content.
  wl_surface* surface() const { return surface_; }

  // |Window|
  int width() const override { return width_; }
  int height() const override { return height_; }
  double scale() const override { return scale_; }
  uint32_t display_id() const override;
  int physical_width() const override;
  int physical_height() const override;
  Role role() const override { return role_; }
  Window* parent() const override { return parent_; }
  bool maximized() const override { return maximized_; }
  bool fullscreen() const override { return fullscreen_; }
  bool activated() const override { return activated_; }
  bool suspended() const override { return suspended_; }
  bool modal() const override { return modal_; }
  bool resizable() const override { return resizable_; }
  const std::string& title() const override { return title_; }
  int popup_x() const override;
  int popup_y() const override;
  int max_suggested_width() const override;
  int max_suggested_height() const override;
  bool has_pointer_focus() const override;
  bool has_keyboard_focus() const override;
  void SetInputHandlers(InputHandler* input_handler,
                        KeyboardHandler* keyboard_handler,
                        InputMethodHandler* input_method_handler) override;
  void SetTitle(const std::string& title) override;
  void SetApplicationId(const std::string& application_id) override;
  void SetSize(int width, int height) override;
  void ResizeToContent(int width, int height) override;
  void SetSizeLimits(const SizeLimits& limits) override;
  void SetMaximized(bool maximized) override;
  void SetMinimized() override;
  void SetFullscreen(bool fullscreen) override;
  void Activate() override;
  void SetParent(Window* parent) override;
  void SetPositioner(const Positioner& positioner) override;
  void StopWaiting() override;

  // |Window|
  //
  // May be called from any thread, but only from one thread at a time.
  //
  // Each frame is shown for at least one display refresh if the compositor
  // supports wp_fifo_v1, rather than being replaced by the next frame.
  //
  // If a popup changes size, it is repositioned first and this blocks until
  // the compositor has placed it, so it moves and resizes in the same frame.
  void Present(RenderBuffer* buffer,
               const SyncPoints* sync,
               const std::vector<Layer>& layers = {}) override;

  // |Window|
  double ContentCornerRadius(int width, int height) override;

  // |Window|
  size_t frames_in_flight() override;

  // Shows the latest buffer of |view|, if it is in this window. Called on
  // the platform thread.
  void CommitPlatformView(WaylandPlatformView* view);

  // Removes |view| from this window. Called on the platform thread.
  void RemovePlatformView(WaylandPlatformView* view);

  // |WindowDecoration::Delegate|
  void OnDecorationFrame(const DecorationFrame& frame) override;
  void OnDecorationClose() override;
  bool IsDecorationBufferPending(RenderBuffer* buffer) override;

 private:
  WaylandWindow(WaylandDisplay* display, Window::Delegate* delegate);

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

  // Whether the window can draw its own decorations.
  bool CanDrawDecorations() const;

  // Draws decorations in the client if |client_side|, otherwise leaves them
  // to the compositor.
  void SetClientSideDecorations(bool client_side);

  // Applies |frame| and sets the window geometry. The changes are shown on
  // the next commit of the surface. Called with |surface_mutex_| held.
  void ApplyDecorationFrame(const DecorationFrame& frame);

  // |WaylandDisplay::OutputObserver|
  void OnOutputsChanged() override;

  // Changes the number of physical pixels per logical pixel.
  void SetScale(double scale);

  // Updates the scale from the outputs the window is on, if the compositor
  // doesn't provide a preferred scale.
  void UpdateOutputScale();

  static void SurfaceEnter(void* data, wl_surface* surface, wl_output* output);
  static void SurfaceLeave(void* data, wl_surface* surface, wl_output* output);
  static void SurfacePreferredBufferScale(void* data,
                                          wl_surface* surface,
                                          int32_t factor);
  static void SurfacePreferredBufferTransform(void* data,
                                              wl_surface* surface,
                                              uint32_t transform);
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
  static void ToplevelDecorationConfigure(
      void* data,
      zxdg_toplevel_decoration_v1* decoration,
      uint32_t mode);
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
  static const zxdg_toplevel_decoration_v1_listener kToplevelDecorationListener;
  static const xdg_activation_token_v1_listener kActivationTokenListener;
  static const wp_fractional_scale_v1_listener kFractionalScaleListener;
  static const wp_presentation_feedback_listener kFeedbackListener;
  static const wl_callback_listener kFrameListener;

  // Destroys |feedback| once it has delivered its result.
  void RemoveFeedback(struct wp_presentation_feedback* feedback);

  WaylandDisplay* display_;
  Window::Delegate* delegate_;
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

  // The IDs of the outputs the surface is shown on, in the order it entered
  // them. May include outputs that have since been removed.
  std::vector<uint32_t> entered_outputs_;

  // The scale the compositor prefers, if it doesn't support fractional
  // scaling but sends wl_surface.preferred_buffer_scale.
  int32_t preferred_buffer_scale_ = 0;

  // The last output reported to the delegate.
  uint32_t last_display_id_ = 0;

  // Draws client-side decorations, or nullptr if the window has none or
  // the compositor draws them. Changed with |surface_mutex_| held.
  std::unique_ptr<WindowDecoration> decoration_;
  DecorationRenderer* decoration_renderer_ = nullptr;
  bool decorated_ = false;

  // Negotiates server-side decorations, if the compositor supports it.
  zxdg_toplevel_decoration_v1* toplevel_decoration_ = nullptr;
  uint32_t pending_decoration_mode_ = 0;

  // True once client-side decorations have set the window geometry, so it
  // must be kept up to date after they are removed. Guarded by
  // |surface_mutex_|.
  bool window_geometry_set_ = false;

  // Held while changing and committing the surfaces.
  std::mutex surface_mutex_;

  // Decorations waiting for content of the size they were drawn for.
  std::optional<DecorationFrame> pending_decoration_;

  // The corner radius of the decorations shown, in logical pixels.
  int content_corner_radius_ = 0;

  // The size of the last content buffer presented, in physical pixels.
  int presented_width_ = 0;
  int presented_height_ = 0;

  // True once a commit has set a fifo barrier. Only accessed by the thread
  // presenting.
  bool fifo_barrier_set_ = false;

  // Created on the first present with explicit synchronization.
  wp_linux_drm_syncobj_surface_v1* syncobj_surface_ = nullptr;

  // A subsurface showing Flutter content above a platform view.
  struct Overlay {
    explicit Overlay(WaylandDisplay* display, wl_surface* parent);
    ~Overlay();

    WaylandDisplay* display;
    wl_surface* surface = nullptr;
    wl_subsurface* subsurface = nullptr;
    wp_viewport* viewport = nullptr;
    wp_linux_drm_syncobj_surface_v1* syncobj_surface = nullptr;
    bool mapped = false;
    SurfacePlacement placement;
    bool placed = false;

    FML_DISALLOW_COPY_AND_ASSIGN(Overlay);
  };

  // Shows |layers| above the content, and hides overlays and platform views
  // that aren't in them. Called with |surface_mutex_| held.
  void PresentLayers(const std::vector<Layer>& layers,
                     int width,
                     int height,
                     bool explicit_sync);

  // Shows |layer| in |overlay| above |above|. Called with |surface_mutex_|
  // held.
  void PresentOverlay(Overlay* overlay,
                      wl_surface* above,
                      const Layer& layer,
                      int width,
                      int height,
                      bool explicit_sync);

  // Overlays, reused between frames. Guarded by |surface_mutex_|.
  std::vector<std::unique_ptr<Overlay>> overlays_;

  // Platform views that have been shown in this window. Guarded by
  // |surface_mutex_|.
  std::vector<std::shared_ptr<WaylandPlatformView>> platform_views_;

  bool configured_ = false;

  // State from the current configure sequence, applied when it completes.
  int pending_width_ = 0;
  int pending_height_ = 0;
  bool pending_maximized_ = false;
  bool pending_fullscreen_ = false;
  bool pending_tiled_ = false;
  bool pending_activated_ = false;
  bool pending_suspended_ = false;
  int pending_popup_x_ = 0;
  int pending_popup_y_ = 0;

  bool maximized_ = false;
  bool fullscreen_ = false;
  bool tiled_ = false;
  bool activated_ = false;
  bool suspended_ = false;
  bool can_minimize_ = true;
  bool can_maximize_ = true;
  bool resizable_ = true;
  bool modal_ = false;
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

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_WINDOW_H_
