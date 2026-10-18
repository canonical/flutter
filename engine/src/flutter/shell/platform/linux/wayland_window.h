// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WINDOW_H_

#include <wayland-client.h>

#include <atomic>
#include <memory>
#include <string>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/wayland_display.h"

namespace flutter {

// A top-level window on a Wayland compositor.
//
// The window is configured on the platform thread, but contents are presented
// from the raster thread. After creation the platform thread never commits
// the surface, so the two threads don't conflict.
class WaylandWindow {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when the window size or scale changes.
    virtual void OnWindowConfigure() = 0;

    // Called when the user requests the window be closed.
    virtual void OnWindowClose() = 0;
  };

  // Creates a window with the given logical size. This blocks until the
  // compositor has configured the window. Returns nullptr on failure.
  static std::unique_ptr<WaylandWindow> Create(WaylandDisplay* display,
                                               Delegate* delegate,
                                               int width,
                                               int height);

  ~WaylandWindow();

  // The logical size of the window.
  int width() const { return width_; }
  int height() const { return height_; }

  // The number of physical pixels per logical pixel.
  double scale() const { return scale_; }

  // The size of the window in physical pixels.
  int physical_width() const;
  int physical_height() const;

  void SetTitle(const std::string& title);
  void SetApplicationId(const std::string& application_id);

  // Shows |buffer| in the window. The buffer is |width| x |height| pixels
  // and rendered with OpenGL conventions (bottom row first).
  //
  // May be called from any thread.
  void Present(wl_buffer* buffer, int width, int height);

 private:
  WaylandWindow(WaylandDisplay* display, Delegate* delegate);

  bool Initialize(int width, int height);

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
  static void PreferredScale(void* data,
                             wp_fractional_scale_v1* fractional_scale,
                             uint32_t scale);

  static const wl_surface_listener kSurfaceListener;
  static const xdg_surface_listener kXdgSurfaceListener;
  static const xdg_toplevel_listener kToplevelListener;
  static const wp_fractional_scale_v1_listener kFractionalScaleListener;

  WaylandDisplay* display_;
  Delegate* delegate_;

  wl_surface* surface_ = nullptr;
  xdg_surface* xdg_surface_ = nullptr;
  xdg_toplevel* toplevel_ = nullptr;
  wp_viewport* viewport_ = nullptr;
  wp_fractional_scale_v1* fractional_scale_ = nullptr;

  bool configured_ = false;

  // State from the current configure sequence, applied when it completes.
  int pending_width_ = 0;
  int pending_height_ = 0;

  std::atomic<int> width_ = 0;
  std::atomic<int> height_ = 0;
  std::atomic<double> scale_ = 1.0;

  FML_DISALLOW_COPY_AND_ASSIGN(WaylandWindow);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WINDOW_H_
