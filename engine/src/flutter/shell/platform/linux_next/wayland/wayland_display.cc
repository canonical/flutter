// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_next/wayland/wayland_display.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux_next/wayland/wayland_window.h"

namespace flutter {

namespace {

// Minimum version of wl_compositor required, for wl_surface.damage_buffer.
constexpr uint32_t kMinCompositorVersion = 4;

// The highest versions of each interface the listeners handle. These must be
// explicit, as binding a newer version would cause the compositor to send
// events the listeners don't handle.
constexpr uint32_t kMaxCompositorVersion = 4;
constexpr uint32_t kMaxWmBaseVersion = 1;
constexpr uint32_t kMaxShmVersion = 1;

template <typename T>
T* Bind(wl_registry* registry,
        uint32_t name,
        const wl_interface* interface,
        uint32_t server_version,
        uint32_t max_version) {
  return static_cast<T*>(wl_registry_bind(
      registry, name, interface, std::min(server_version, max_version)));
}

}  // namespace

const wl_registry_listener WaylandDisplay::kRegistryListener = {
    .global = RegistryGlobal,
    .global_remove = RegistryGlobalRemove,
};

const xdg_wm_base_listener WaylandDisplay::kWmBaseListener = {
    .ping = WmBasePing,
};

std::unique_ptr<WaylandDisplay> WaylandDisplay::Create(
    EventLoop* event_loop,
    FlutterLinuxResult* result) {
  wl_display* display = wl_display_connect(nullptr);
  if (display == nullptr) {
    FML_LOG(ERROR) << "Failed to connect to Wayland compositor: "
                   << strerror(errno);
    *result = kFlutterLinuxResultNoDisplay;
    return nullptr;
  }

  std::unique_ptr<WaylandDisplay> wayland_display(
      new WaylandDisplay(display, event_loop));
  *result = wayland_display->Initialize();
  if (*result != kFlutterLinuxResultSuccess) {
    return nullptr;
  }
  return wayland_display;
}

WaylandDisplay::WaylandDisplay(wl_display* display, EventLoop* event_loop)
    : display_(display), event_loop_(event_loop) {}

WaylandDisplay::~WaylandDisplay() {
  event_loop_->RemoveSource(this);
  if (read_prepared_) {
    wl_display_cancel_read(display_);
  }
  if (shm_ != nullptr) {
    wl_shm_destroy(shm_);
  }
  if (wm_base_ != nullptr) {
    xdg_wm_base_destroy(wm_base_);
  }
  if (compositor_ != nullptr) {
    wl_compositor_destroy(compositor_);
  }
  if (registry_ != nullptr) {
    wl_registry_destroy(registry_);
  }
  wl_display_disconnect(display_);
}

FlutterLinuxResult WaylandDisplay::Initialize() {
  registry_ = wl_display_get_registry(display_);
  wl_registry_add_listener(registry_, &kRegistryListener, this);
  if (wl_display_roundtrip(display_) < 0) {
    FML_LOG(ERROR) << "Failed to get Wayland globals";
    return kFlutterLinuxResultFailed;
  }

  if (compositor_ == nullptr) {
    FML_LOG(ERROR) << "Wayland compositor does not support wl_compositor v"
                   << kMinCompositorVersion;
    return kFlutterLinuxResultUnsupportedDisplay;
  }
  if (wm_base_ == nullptr) {
    FML_LOG(ERROR) << "Wayland compositor does not support xdg_wm_base";
    return kFlutterLinuxResultUnsupportedDisplay;
  }
  if (shm_ == nullptr) {
    FML_LOG(ERROR) << "Wayland compositor does not support wl_shm";
    return kFlutterLinuxResultUnsupportedDisplay;
  }

  event_loop_->AddSource(this);
  return kFlutterLinuxResultSuccess;
}

std::unique_ptr<Window> WaylandDisplay::CreateWindow(
    Window::Delegate* delegate,
    const Window::Params& params) {
  return std::make_unique<WaylandWindow>(this, delegate, params);
}

int WaylandDisplay::GetFd() const {
  return wl_display_get_fd(display_);
}

void WaylandDisplay::Prepare() {
  if (failed_) {
    return;
  }

  // Events can't be read until everything already queued is dispatched.
  while (wl_display_prepare_read(display_) != 0) {
    if (wl_display_dispatch_pending(display_) < 0) {
      HandleError();
      return;
    }
  }
  read_prepared_ = true;

  // Send any requests made since the last iteration. If the socket buffer is
  // full the remainder is sent on the next iteration.
  if (wl_display_flush(display_) < 0 && errno != EAGAIN) {
    HandleError();
  }
}

void WaylandDisplay::Dispatch(bool readable) {
  if (read_prepared_) {
    read_prepared_ = false;
    if (readable) {
      if (wl_display_read_events(display_) < 0) {
        HandleError();
        return;
      }
    } else {
      wl_display_cancel_read(display_);
    }
  }

  if (failed_) {
    return;
  }
  if (wl_display_dispatch_pending(display_) < 0) {
    HandleError();
  }
}

void WaylandDisplay::HandleError() {
  if (read_prepared_) {
    wl_display_cancel_read(display_);
    read_prepared_ = false;
  }
  if (failed_) {
    return;
  }
  failed_ = true;

  int error = wl_display_get_error(display_);
  FML_LOG(ERROR) << "Lost connection to Wayland compositor: "
                 << strerror(error != 0 ? error : errno);
  if (error_callback_) {
    error_callback_();
  }
}

void WaylandDisplay::RegistryGlobal(void* data,
                                    wl_registry* registry,
                                    uint32_t name,
                                    const char* interface,
                                    uint32_t version) {
  auto* self = static_cast<WaylandDisplay*>(data);
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    if (version >= kMinCompositorVersion) {
      self->compositor_ =
          Bind<wl_compositor>(registry, name, &wl_compositor_interface, version,
                              kMaxCompositorVersion);
    }
  } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
    self->wm_base_ = Bind<xdg_wm_base>(registry, name, &xdg_wm_base_interface,
                                       version, kMaxWmBaseVersion);
    xdg_wm_base_add_listener(self->wm_base_, &kWmBaseListener, self);
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    self->shm_ = Bind<wl_shm>(registry, name, &wl_shm_interface, version,
                              kMaxShmVersion);
  }
}

void WaylandDisplay::RegistryGlobalRemove(void* data,
                                          wl_registry* registry,
                                          uint32_t name) {}

void WaylandDisplay::WmBasePing(void* data,
                                xdg_wm_base* wm_base,
                                uint32_t serial) {
  xdg_wm_base_pong(wm_base, serial);
}

}  // namespace flutter
