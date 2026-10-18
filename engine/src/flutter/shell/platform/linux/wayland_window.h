// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WINDOW_H_

#include <wayland-client.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/decoration_renderer.h"
#include "flutter/shell/platform/linux/wayland_display.h"
#include "flutter/shell/platform/linux/window_decoration.h"

namespace flutter {

// A top-level window on a Wayland compositor.
//
// The window is configured on the platform thread, but contents are presented
// from the raster thread. The platform thread only commits the surface to
// update the decorations, synchronized with presentation by a lock.
//
// If |decoration_renderer| is provided the window draws its own title bar
// and shadow in a subsurface below the content. Decorations of a new size
// are shown with the first content buffer of the matching size, so resizing
// is atomic and presenting content doesn't redraw the decorations.
class WaylandWindow : public WindowDecoration::Delegate {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when the window size or scale changes.
    virtual void OnWindowConfigure() = 0;

    // Called when the user requests the window be closed.
    virtual void OnWindowClose() = 0;

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

  // Creates a window with content of the given logical size. This blocks
  // until the compositor has configured the window. |decoration_renderer|
  // may be nullptr, in which case the window is undecorated. Returns nullptr
  // on failure.
  static std::unique_ptr<WaylandWindow> Create(
      WaylandDisplay* display,
      DecorationRenderer* decoration_renderer,
      Delegate* delegate,
      int width,
      int height);

  ~WaylandWindow() override;

  // The logical size of the window content, excluding decorations.
  int width() const { return width_; }
  int height() const { return height_; }

  // The number of physical pixels per logical pixel.
  double scale() const { return scale_; }

  // The size of the window content in physical pixels.
  int physical_width() const;
  int physical_height() const;

  void SetTitle(const std::string& title);
  void SetApplicationId(const std::string& application_id);

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
                  int width,
                  int height);

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
  static const wp_fractional_scale_v1_listener kFractionalScaleListener;
  static const wp_presentation_feedback_listener kFeedbackListener;
  static const wl_callback_listener kFrameListener;

  // Destroys |feedback| once it has delivered its result.
  void RemoveFeedback(struct wp_presentation_feedback* feedback);

  WaylandDisplay* display_;
  Delegate* delegate_;

  wl_surface* surface_ = nullptr;
  xdg_surface* xdg_surface_ = nullptr;
  xdg_toplevel* toplevel_ = nullptr;
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

  bool maximized_ = false;
  bool fullscreen_ = false;
  bool tiled_ = false;
  bool activated_ = false;
  bool can_minimize_ = true;
  bool can_maximize_ = true;
  std::string title_;

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
