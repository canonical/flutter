// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_DESKTOP_PLUGIN_REGISTRAR_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_DESKTOP_PLUGIN_REGISTRAR_H_

#include <cstdint>
#include <map>
#include <memory>
#include <set>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/common/public/flutter_plugin_registrar.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/flutter_desktop_messenger.h"
#include "flutter/shell/platform/linux/public/flutter_linux_abi.h"

namespace flutter {
class FlutterLinuxView;
}  // namespace flutter

// The registrar behind FlutterDesktopPluginRegistrarRef, which gives a plugin
// access to an engine. Each plugin has its own registrar in each engine.
//
// Used on the platform thread, except PostTask.
struct FlutterDesktopPluginRegistrar {
 public:
  using Views = std::map<int64_t, flutter::FlutterLinuxView*>;

  // Creates a registrar for an engine that sends messages with |messenger|,
  // runs on |event_loop| and shows |views|. These must outlive the registrar.
  FlutterDesktopPluginRegistrar(FlutterDesktopMessenger* messenger,
                                flutter::EventLoop* event_loop,
                                wl_display* display,
                                const Views* views);

  // Removes all watches.
  ~FlutterDesktopPluginRegistrar();

  FlutterDesktopPluginRegistrarRef ToRef() { return this; }

  FlutterDesktopMessenger* messenger() const { return messenger_; }
  wl_display* display() const { return display_; }

  // Returns the view with |view_id|, or nullptr.
  flutter::FlutterLinuxView* GetView(int64_t view_id) const;

  void SetDestructionHandler(FlutterDesktopOnPluginRegistrarDestroyed handler) {
    destruction_handler_ = handler;
  }

  // Calls the destruction handler, which destroys the plugin.
  void NotifyDestroyed();

  void SetViewCallbacks(FlutterLinuxViewCallback added,
                        FlutterLinuxViewCallback removed,
                        void* user_data);

  // Called by the engine when it adds or removes views.
  void OnViewAdded(int64_t view_id);
  void OnViewRemoved(int64_t view_id);

  // May be called from any thread.
  void PostTask(uint64_t delay_ns,
                FlutterLinuxCallback callback,
                FlutterLinuxCallback destroy,
                void* user_data);

  uint64_t AddFdWatch(int fd,
                      uint32_t events,
                      FlutterLinuxFdCallback callback,
                      FlutterLinuxCallback destroy,
                      void* user_data);
  bool SetFdWatchEvents(uint64_t watch_id, uint32_t events);
  void RemoveFdWatch(uint64_t watch_id);

 private:
  FlutterDesktopMessenger* messenger_;
  flutter::EventLoop* event_loop_;
  wl_display* display_;
  const Views* views_;

  FlutterDesktopOnPluginRegistrarDestroyed destruction_handler_ = nullptr;

  FlutterLinuxViewCallback view_added_ = nullptr;
  FlutterLinuxViewCallback view_removed_ = nullptr;
  void* view_user_data_ = nullptr;

  // The event loop IDs of the plugin's file descriptor watches.
  std::set<uint64_t> watches_;

  // Tasks hold a weak reference to this, so they don't run after the
  // registrar is destroyed.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterDesktopPluginRegistrar);
};

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_DESKTOP_PLUGIN_REGISTRAR_H_
