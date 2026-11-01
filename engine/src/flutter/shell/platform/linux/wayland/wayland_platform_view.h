// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_PLATFORM_VIEW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_PLATFORM_VIEW_H_

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/platform_view.h"
#include "flutter/shell/platform/linux/platform_view_geometry.h"
#include "flutter/shell/platform/linux/wayland/wayland_display.h"

namespace flutter {

class WaylandWindow;

// A platform view shown in its own Wayland subsurface, so the compositor can
// put its buffers on screen without them being drawn by Flutter. Flutter
// content above a platform view is shown in overlay subsurfaces. The embedder
// places, crops and stacks the surface to match the Flutter scene.
//
// The window presenting the view calls the methods below on the raster
// thread, with its surface lock held.
class WaylandPlatformView : public LinuxPlatformView {
 public:
  WaylandPlatformView(int64_t id,
                      WaylandDisplay* display,
                      EventLoop* event_loop);
  ~WaylandPlatformView() override;

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

 protected:
  // |LinuxPlatformView|
  std::unique_ptr<Buffer> CreateBackendBuffer(
      const DmabufFrame& frame) override;
  void OnPresent() override;
  void OnDispose() override;
  void OnBufferRemoved(Buffer* buffer) override;

 private:
  class WaylandBuffer;

  // Destroys the subsurface. Called with |mutex_| held.
  void DestroySubsurface();

  // Updates the surface to show |current_| at |geometry_|, or hides it.
  // Called with |mutex_| held.
  void Apply();

  // Called when the compositor has finished with a buffer.
  void OnBufferReleased(WaylandBuffer* buffer);

  WaylandDisplay* display_;

  // The view's surface, created when the view is shown in a window.
  wl_surface* surface_ = nullptr;
  wl_subsurface* subsurface_ = nullptr;
  wp_viewport* viewport_ = nullptr;
  wp_alpha_modifier_surface_v1* alpha_ = nullptr;

  // The window the subsurface is in, or nullptr.
  WaylandWindow* window_ = nullptr;

  // The buffer attached to the surface, or nullptr if it has been freed or
  // the surface is hidden.
  Buffer* attached_ = nullptr;

  // True if the surface has a buffer attached.
  bool mapped_ = false;

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

  // Warnings logged once per view.
  bool warned_approximated_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(WaylandPlatformView);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_PLATFORM_VIEW_H_
