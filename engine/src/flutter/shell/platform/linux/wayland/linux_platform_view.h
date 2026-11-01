// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_LINUX_PLATFORM_VIEW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_LINUX_PLATFORM_VIEW_H_

#include <functional>
#include <memory>
#include <mutex>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/external_texture.h"
#include "flutter/shell/platform/linux/platform_view_geometry.h"
#include "flutter/shell/platform/linux/wayland/wayland_display.h"

namespace flutter {

class WaylandWindow;

// A pointer event in a platform view, forwarded from the framework.
struct PlatformViewPointerEvent {
  enum class Kind {
    kDown,
    kMove,
    kUp,
    kCancel,
    kHover,
    kExit,
    kScroll,
  };
  enum class DeviceKind {
    kMouse,
    kTouch,
    kStylus,
    kTrackpad,
  };

  Kind kind = Kind::kMove;
  DeviceKind device_kind = DeviceKind::kMouse;
  int32_t device = 0;
  int64_t buttons = 0;

  // The position in logical pixels, relative to the view's top left.
  double x = 0;
  double y = 0;

  double scroll_delta_x = 0;
  double scroll_delta_y = 0;

  int64_t timestamp_us = 0;
};

// A native view shown in a Flutter view, such as a video or web page.
//
// Platform views are shown in their own Wayland subsurface, so the
// compositor can put their buffers on screen without them being drawn by
// Flutter. Flutter content above a platform view is shown in overlay
// subsurfaces. The plugin providing the view creates buffers from dmabufs and
// presents them, and the embedder places, crops and stacks the surface to
// match the Flutter scene.
//
// Plugins call methods on the platform thread. The window presenting the
// view calls the methods marked as such on the raster thread, with its
// surface lock held.
class LinuxPlatformView
    : public std::enable_shared_from_this<LinuxPlatformView> {
 public:
  // A buffer that can be presented in the view, created from a dmabuf.
  class Buffer {
   public:
    ~Buffer();

    int width() const { return width_; }
    int height() const { return height_; }

   private:
    friend class LinuxPlatformView;

    Buffer(std::shared_ptr<LinuxPlatformView> view,
           wl_buffer* buffer,
           int width,
           int height,
           std::function<void()> release);

    static void OnRelease(void* data, wl_buffer* buffer);
    static const wl_buffer_listener kListener;

    std::shared_ptr<LinuxPlatformView> view_;
    wl_buffer* buffer_;
    int width_;
    int height_;
    std::function<void()> release_;

    // True while the compositor may be reading the buffer. Guarded by the
    // view's mutex.
    bool busy_ = false;

    // True if the plugin is waiting for the buffer to be released. Guarded
    // by the view's mutex.
    bool release_pending_ = false;

    FML_DISALLOW_COPY_AND_ASSIGN(Buffer);
  };

  // Called on the platform thread when the size the view is shown at
  // changes, in physical pixels, with the number of physical pixels per
  // logical pixel.
  using ResizeCallback =
      std::function<void(int width, int height, double scale)>;
  using PointerCallback = std::function<void(const PlatformViewPointerEvent&)>;

  LinuxPlatformView(int64_t id, WaylandDisplay* display, EventLoop* event_loop);
  ~LinuxPlatformView();

  int64_t id() const { return id_; }

  void SetCallbacks(ResizeCallback on_resize, PointerCallback on_pointer);

  // Creates a buffer from |frame|. The planes' file descriptors are not
  // kept. |frame.release| is called on the platform thread once the buffer
  // has been replaced by another one and the compositor has finished with
  // it. Returns nullptr if the compositor doesn't support the buffer's
  // format.
  std::unique_ptr<Buffer> CreateBuffer(const DmabufFrame& frame);

  // Shows |buffer| in the view. Replaces the previous buffer, which is
  // released once the compositor has finished with it. The buffer is shown
  // immediately if the view is visible, otherwise when it is next drawn.
  void Present(Buffer* buffer);

  // Forwards a pointer event from the framework to the plugin.
  void DispatchPointerEvent(const PlatformViewPointerEvent& event);

  // Removes the view. Called when the framework disposes it. Buffers may
  // remain until the plugin frees them.
  void Dispose();

  // Shows the view in |window| with its surface placed above |above| (a
  // child of |parent|) at |geometry|, where |scale| is the number of
  // physical pixels per logical pixel. Stacking and position changes apply
  // when |parent| is committed. Called with the window's surface lock held.
  // Returns the view's surface, or nullptr if it couldn't be shown.
  wl_surface* Show(WaylandWindow* window,
                   wl_surface* parent,
                   wl_surface* above,
                   const PlatformViewGeometry& geometry,
                   double scale);

  // Hides the view if it is in |window|. Called with the window's surface
  // lock held.
  void Hide(WaylandWindow* window);

  // Removes the view from |window|, if it is in it. Called with the
  // window's surface lock held.
  void Detach(WaylandWindow* window);

  // True if the view's surface is in |window|. Called with the window's
  // surface lock held.
  bool IsInWindow(WaylandWindow* window);

  // Commits the current buffer if the view is shown in |window|. Returns
  // true if the parent surface needs committing to show it. Called with the
  // window's surface lock held.
  bool CommitBuffer(WaylandWindow* window);

 private:
  // Destroys the subsurface. Called with |mutex_| held.
  void DestroySubsurface();

  // Updates the surface to show |current_| at |geometry_|, or hides it.
  // Called with |mutex_| held.
  void Apply();

  // True if |buffer| has been replaced and the compositor has finished with
  // it, so its release callback should be called. Called with |mutex_| held.
  bool TakeRelease(Buffer* buffer);

  // Called when a buffer is freed by the plugin.
  void RemoveBuffer(Buffer* buffer);

  // Called when the compositor has finished with a buffer.
  void OnBufferReleased(Buffer* buffer);

  int64_t id_;
  WaylandDisplay* display_;
  EventLoop* event_loop_;

  ResizeCallback on_resize_;
  PointerCallback on_pointer_;

  std::mutex mutex_;

  // The view's surface, created when the view is shown in a window.
  wl_surface* surface_ = nullptr;
  wl_subsurface* subsurface_ = nullptr;
  wp_viewport* viewport_ = nullptr;
  wp_alpha_modifier_surface_v1* alpha_ = nullptr;

  // The window the subsurface is in, or nullptr.
  WaylandWindow* window_ = nullptr;

  // True once disposed, after which the view isn't shown.
  bool disposed_ = false;

  // The buffer the plugin last presented, or nullptr.
  Buffer* current_ = nullptr;

  // The buffer attached to the surface, or nullptr if it has been freed or
  // the surface is hidden.
  Buffer* attached_ = nullptr;

  // True if the surface has a buffer attached.
  bool mapped_ = false;

  // Buffers that exist, so the release of replaced buffers can be tracked.
  std::vector<Buffer*> buffers_;

  // True if the view was in the last frame of |window_|.
  bool shown_ = false;

  // Where the view was last shown in |window_|, in physical pixels, and
  // the window's scale.
  PlatformViewGeometry geometry_;
  double scale_ = 1.0;

  // How the surface was last placed, to only send changes.
  SurfacePlacement placement_;
  double opacity_ = 1.0;
  bool placed_ = false;

  // The size last sent to the platform thread, guarded by |mutex_|.
  int reported_width_ = 0;
  int reported_height_ = 0;
  double reported_scale_ = 0;

  // The size the view is shown at, only used on the platform thread.
  int shown_width_ = 0;
  int shown_height_ = 0;
  double shown_scale_ = 0;

  // Warnings logged once per view.
  bool warned_approximated_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(LinuxPlatformView);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_LINUX_PLATFORM_VIEW_H_
