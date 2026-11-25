// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_NEXT_WAYLAND_WAYLAND_DISPLAY_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_NEXT_WAYLAND_WAYLAND_DISPLAY_H_

#include <functional>
#include <memory>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux_next/event_loop.h"
#include "flutter/shell/platform/linux_next/public/flutter_linux_result.h"
#include "flutter/shell/platform/linux_next/window.h"
#include "flutter/third_party/wayland_protocols/wayland-client-protocol.h"
#include "flutter/third_party/wayland_protocols/xdg-shell-client-protocol.h"

namespace flutter {

// A connection to a Wayland compositor.
//
// Events from the compositor are dispatched by |event_loop|, which must
// outlive this object.
class WaylandDisplay : public EventLoop::Source {
 public:
  // Connects to the compositor. Returns nullptr if there is no compositor or
  // it doesn't support the required protocols. Sets |result| to the result.
  static std::unique_ptr<WaylandDisplay> Create(EventLoop* event_loop,
                                                FlutterLinuxResult* result);

  ~WaylandDisplay() override;

  wl_display* display() const { return display_; }
  wl_compositor* compositor() const { return compositor_; }
  wl_shm* shm() const { return shm_; }
  xdg_wm_base* wm_base() const { return wm_base_; }

  // Creates a top-level window. |delegate| must outlive the window.
  std::unique_ptr<Window> CreateWindow(Window::Delegate* delegate,
                                       const Window::Params& params);

  // Sets |callback| to be called if the connection to the compositor is lost.
  void SetErrorCallback(std::function<void()> callback) {
    error_callback_ = std::move(callback);
  }

  // |EventLoop::Source|
  int GetFd() const override;

  // |EventLoop::Source|
  void Prepare() override;

  // |EventLoop::Source|
  void Dispatch(bool readable) override;

 private:
  WaylandDisplay(wl_display* display, EventLoop* event_loop);

  FlutterLinuxResult Initialize();

  // Stops processing events after a protocol or connection error.
  void HandleError();

  static void RegistryGlobal(void* data,
                             wl_registry* registry,
                             uint32_t name,
                             const char* interface,
                             uint32_t version);
  static void RegistryGlobalRemove(void* data,
                                   wl_registry* registry,
                                   uint32_t name);
  static void WmBasePing(void* data, xdg_wm_base* wm_base, uint32_t serial);

  static const wl_registry_listener kRegistryListener;
  static const xdg_wm_base_listener kWmBaseListener;

  wl_display* display_;
  EventLoop* event_loop_;
  wl_registry* registry_ = nullptr;
  wl_compositor* compositor_ = nullptr;
  wl_shm* shm_ = nullptr;
  xdg_wm_base* wm_base_ = nullptr;

  // True between preparing to read events and reading or cancelling.
  bool read_prepared_ = false;

  // True once the connection has failed.
  bool failed_ = false;

  std::function<void()> error_callback_;

  FML_DISALLOW_COPY_AND_ASSIGN(WaylandDisplay);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_NEXT_WAYLAND_WAYLAND_DISPLAY_H_
