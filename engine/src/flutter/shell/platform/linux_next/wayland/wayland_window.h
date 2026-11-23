// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_NEXT_WAYLAND_WAYLAND_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_NEXT_WAYLAND_WAYLAND_WINDOW_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux_next/wayland/shm_buffer.h"
#include "flutter/shell/platform/linux_next/wayland/wayland_display.h"
#include "flutter/shell/platform/linux_next/window.h"
#include "flutter/third_party/wayland_protocols/wayland-client-protocol.h"
#include "flutter/third_party/wayland_protocols/xdg-shell-client-protocol.h"

namespace flutter {

// A top level window on a Wayland compositor.
//
// Must be used on the thread that dispatches |display|'s events.
class WaylandWindow : public Window {
 public:
  // Creates a window. |display| and |delegate| must outlive the window.
  WaylandWindow(WaylandDisplay* display,
                Delegate* delegate,
                const Params& params);
  ~WaylandWindow() override;

  // |Window|
  int width() const override { return width_; }
  int height() const override { return height_; }
  double scale() const override { return 1.0; }
  void SetTitle(const std::string& title) override;
  void Present(const uint8_t* pixels,
               int width,
               int height,
               size_t row_bytes) override;

 private:
  // Returns a buffer that isn't in use by the compositor of the given size,
  // or nullptr if too many buffers are in use.
  ShmBuffer* GetFreeBuffer(int width, int height);

  // Shows the pending frame if there is one and a buffer is available.
  void PresentPendingFrame();

  static void XdgSurfaceConfigure(void* data,
                                  xdg_surface* xdg_surface,
                                  uint32_t serial);
  static void XdgToplevelConfigure(void* data,
                                   xdg_toplevel* xdg_toplevel,
                                   int32_t width,
                                   int32_t height,
                                   wl_array* states);
  static void XdgToplevelClose(void* data, xdg_toplevel* xdg_toplevel);

  static const xdg_surface_listener kXdgSurfaceListener;
  static const xdg_toplevel_listener kXdgToplevelListener;

  WaylandDisplay* display_;
  Delegate* delegate_;
  wl_surface* surface_ = nullptr;
  xdg_surface* xdg_surface_ = nullptr;
  xdg_toplevel* xdg_toplevel_ = nullptr;

  int width_;
  int height_;

  // The size from the last toplevel configure event, applied when the
  // surface configure event completes the sequence.
  int pending_width_ = 0;
  int pending_height_ = 0;

  // True once the compositor has sent the first configure event. Buffers
  // can't be attached before then.
  bool configured_ = false;

  std::vector<std::unique_ptr<ShmBuffer>> buffers_;

  // A frame waiting to be shown because the window isn't configured yet or
  // all the buffers are in use.
  std::vector<uint8_t> pending_frame_;
  int pending_frame_width_ = 0;
  int pending_frame_height_ = 0;

  FML_DISALLOW_COPY_AND_ASSIGN(WaylandWindow);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_NEXT_WAYLAND_WAYLAND_WINDOW_H_
