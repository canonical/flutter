// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_CLIENT_WRAPPER_INCLUDE_FLUTTER_PLUGIN_REGISTRAR_LINUX_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_CLIENT_WRAPPER_INCLUDE_FLUTTER_PLUGIN_REGISTRAR_LINUX_H_

#include <flutter_linux_abi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "flutter_view.h"
#include "plugin_registrar.h"

namespace flutter {

// Receives notifications when views are added to or removed from an engine.
class ViewObserver {
 public:
  virtual ~ViewObserver() = default;

  // Called when |view| is added.
  virtual void OnViewAdded(FlutterView* view) {}

  // Called before |view| is removed. It is destroyed after this returns.
  virtual void OnViewRemoved(FlutterView* view) {}
};

// An extension to PluginRegistrar providing access to Linux-specific
// functionality.
//
// Each plugin has its own registrar in each engine, so a plugin is created
// once per engine and must not keep global state. Plugins are destroyed with
// their engine. All methods are called on the platform thread, except where
// noted.
//
// An engine can show multiple views, so methods that need a view (e.g. to
// parent a dialog) should be passed the view ID from Dart
// (`View.of(context).viewId`) and look it up with GetViewById.
class PluginRegistrarLinux : public PluginRegistrar {
 public:
  // File descriptor events for AddFdWatch.
  static constexpr uint32_t kFdReadable = FLUTTER_LINUX_FD_EVENT_READABLE;
  static constexpr uint32_t kFdWritable = FLUTTER_LINUX_FD_EVENT_WRITABLE;
  // Always reported, even if not requested.
  static constexpr uint32_t kFdError = FLUTTER_LINUX_FD_EVENT_ERROR;
  static constexpr uint32_t kFdHangUp = FLUTTER_LINUX_FD_EVENT_HANG_UP;

  using FdCallback = std::function<void(uint32_t events)>;

  // Creates a new PluginRegistrar. |core_registrar| and the messenger it
  // provides must remain valid as long as this object exists.
  explicit PluginRegistrarLinux(FlutterDesktopPluginRegistrarRef core_registrar)
      : PluginRegistrar(core_registrar) {
    flutter_linux_plugin_registrar_set_view_callbacks(
        core_registrar, ViewAddedCallback, ViewRemovedCallback, this);
  }

  ~PluginRegistrarLinux() override {
    // Must be the first call.
    ClearPlugins();
    flutter_linux_plugin_registrar_set_view_callbacks(registrar(), nullptr,
                                                      nullptr, nullptr);
  }

  // Prevent copying.
  PluginRegistrarLinux(PluginRegistrarLinux const&) = delete;
  PluginRegistrarLinux& operator=(PluginRegistrarLinux const&) = delete;

  // The connection to the Wayland compositor, shared by all engines. Plugins
  // may create their own event queues on it to use it from other threads.
  wl_display* display() const {
    return flutter_linux_plugin_registrar_get_wl_display(registrar());
  }

  // Returns the view with |view_id|, or nullptr if it doesn't exist. The view
  // is valid until ViewObserver::OnViewRemoved is called for it.
  FlutterView* GetViewById(int64_t view_id) const {
    auto it = views_.find(view_id);
    return it == views_.end() ? nullptr : it->second.get();
  }

  // Adds an observer to be notified when views are added and removed. It is
  // not called for views that already exist. |observer| must be removed
  // before it is destroyed.
  void AddViewObserver(ViewObserver* observer) {
    view_observers_.push_back(observer);
  }

  void RemoveViewObserver(ViewObserver* observer) {
    view_observers_.erase(
        std::remove(view_observers_.begin(), view_observers_.end(), observer),
        view_observers_.end());
  }

  // Runs |task| on the platform thread after |delay|. The task is dropped if
  // the engine is destroyed first.
  //
  // May be called from any thread while the plugin exists, e.g. to return
  // results from a worker thread.
  void PostTask(std::function<void()> task,
                std::chrono::nanoseconds delay = std::chrono::nanoseconds(0)) {
    flutter_linux_plugin_registrar_post_task(
        registrar(),
        static_cast<uint64_t>(
            std::max<std::chrono::nanoseconds::rep>(delay.count(), 0)),
        [](void* user_data) {
          (*static_cast<std::function<void()>*>(user_data))();
        },
        [](void* user_data) {
          delete static_cast<std::function<void()>*>(user_data);
        },
        new std::function<void()>(std::move(task)));
  }

  // Calls |callback| on the platform thread when any of |events| (kFd*
  // flags) occur on |fd|, which must remain open until the watch is removed.
  // Returns an ID for the watch, or zero on failure. Watches are removed when
  // the engine is destroyed.
  uint64_t AddFdWatch(int fd, uint32_t events, FdCallback callback) {
    return flutter_linux_plugin_registrar_add_fd_watch(
        registrar(), fd, events,
        [](int fd, uint32_t events, void* user_data) {
          (*static_cast<FdCallback*>(user_data))(events);
        },
        [](void* user_data) { delete static_cast<FdCallback*>(user_data); },
        new FdCallback(std::move(callback)));
  }

  // Changes the events the watch with |watch_id| waits for.
  bool SetFdWatchEvents(uint64_t watch_id, uint32_t events) {
    return flutter_linux_plugin_registrar_set_fd_watch_events(registrar(),
                                                              watch_id, events);
  }

  // Removes the watch with |watch_id|. May be called from its callback.
  void RemoveFdWatch(uint64_t watch_id) {
    flutter_linux_plugin_registrar_remove_fd_watch(registrar(), watch_id);
  }

 private:
  static void ViewAddedCallback(int64_t view_id, void* user_data) {
    auto* self = static_cast<PluginRegistrarLinux*>(user_data);
    FlutterLinuxView* view =
        flutter_linux_plugin_registrar_get_view(self->registrar(), view_id);
    if (view == nullptr) {
      return;
    }
    auto& entry = self->views_[view_id];
    entry = std::make_unique<FlutterView>(view_id, view);
    // Copied so observers can remove themselves.
    std::vector<ViewObserver*> observers = self->view_observers_;
    for (ViewObserver* observer : observers) {
      observer->OnViewAdded(entry.get());
    }
  }

  static void ViewRemovedCallback(int64_t view_id, void* user_data) {
    auto* self = static_cast<PluginRegistrarLinux*>(user_data);
    auto it = self->views_.find(view_id);
    if (it == self->views_.end()) {
      return;
    }
    std::vector<ViewObserver*> observers = self->view_observers_;
    for (ViewObserver* observer : observers) {
      observer->OnViewRemoved(it->second.get());
    }
    self->views_.erase(view_id);
  }

  std::map<int64_t, std::unique_ptr<FlutterView>> views_;
  std::vector<ViewObserver*> view_observers_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_CLIENT_WRAPPER_INCLUDE_FLUTTER_PLUGIN_REGISTRAR_LINUX_H_
