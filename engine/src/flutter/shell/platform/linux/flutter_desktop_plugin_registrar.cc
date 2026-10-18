// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/flutter_desktop_plugin_registrar.h"

#include <sys/epoll.h>

#include <chrono>
#include <utility>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/common/public/flutter_texture_registrar.h"

namespace {

// Calls a destroy notify when the callback it belongs to is destroyed.
class DestroyNotify {
 public:
  DestroyNotify(FlutterLinuxCallback destroy, void* user_data)
      : destroy_(destroy), user_data_(user_data) {}
  ~DestroyNotify() {
    if (destroy_ != nullptr) {
      destroy_(user_data_);
    }
  }

 private:
  FlutterLinuxCallback destroy_;
  void* user_data_;

  FML_DISALLOW_COPY_AND_ASSIGN(DestroyNotify);
};

uint32_t ToEpollEvents(uint32_t events) {
  uint32_t result = 0;
  if (events & FLUTTER_LINUX_FD_EVENT_READABLE) {
    result |= EPOLLIN;
  }
  if (events & FLUTTER_LINUX_FD_EVENT_WRITABLE) {
    result |= EPOLLOUT;
  }
  return result;
}

uint32_t FromEpollEvents(uint32_t events) {
  uint32_t result = 0;
  if (events & EPOLLIN) {
    result |= FLUTTER_LINUX_FD_EVENT_READABLE;
  }
  if (events & EPOLLOUT) {
    result |= FLUTTER_LINUX_FD_EVENT_WRITABLE;
  }
  if (events & EPOLLERR) {
    result |= FLUTTER_LINUX_FD_EVENT_ERROR;
  }
  if (events & EPOLLHUP) {
    result |= FLUTTER_LINUX_FD_EVENT_HANG_UP;
  }
  return result;
}

}  // namespace

FlutterDesktopPluginRegistrar::FlutterDesktopPluginRegistrar(
    FlutterDesktopMessenger* messenger,
    flutter::EventLoop* event_loop,
    wl_display* display,
    const Views* views)
    : messenger_(messenger),
      event_loop_(event_loop),
      display_(display),
      views_(views) {}

FlutterDesktopPluginRegistrar::~FlutterDesktopPluginRegistrar() {
  for (uint64_t id : watches_) {
    event_loop_->RemoveWatch(id);
  }
}

flutter::FlutterLinuxView* FlutterDesktopPluginRegistrar::GetView(
    int64_t view_id) const {
  auto it = views_->find(view_id);
  return it == views_->end() ? nullptr : it->second;
}

void FlutterDesktopPluginRegistrar::NotifyDestroyed() {
  FlutterDesktopOnPluginRegistrarDestroyed handler = destruction_handler_;
  destruction_handler_ = nullptr;
  if (handler != nullptr) {
    handler(ToRef());
  }
}

void FlutterDesktopPluginRegistrar::SetViewCallbacks(
    FlutterLinuxViewCallback added,
    FlutterLinuxViewCallback removed,
    void* user_data) {
  view_added_ = added;
  view_removed_ = removed;
  view_user_data_ = user_data;
  if (view_added_ == nullptr) {
    return;
  }
  // Copy the IDs in case the callback changes the views.
  std::vector<int64_t> view_ids;
  for (const auto& [view_id, view] : *views_) {
    view_ids.push_back(view_id);
  }
  for (int64_t view_id : view_ids) {
    if (views_->count(view_id) != 0 && view_added_ != nullptr) {
      view_added_(view_id, view_user_data_);
    }
  }
}

void FlutterDesktopPluginRegistrar::OnViewAdded(int64_t view_id) {
  if (view_added_ != nullptr) {
    view_added_(view_id, view_user_data_);
  }
}

void FlutterDesktopPluginRegistrar::OnViewRemoved(int64_t view_id) {
  if (view_removed_ != nullptr) {
    view_removed_(view_id, view_user_data_);
  }
}

void FlutterDesktopPluginRegistrar::PostTask(uint64_t delay_ns,
                                             FlutterLinuxCallback callback,
                                             FlutterLinuxCallback destroy,
                                             void* user_data) {
  auto notify = std::make_shared<DestroyNotify>(destroy, user_data);
  event_loop_->PostTask(
      [alive = std::weak_ptr<bool>(alive_), notify = std::move(notify),
       callback, user_data]() {
        if (!alive.expired()) {
          callback(user_data);
        }
      },
      flutter::EventLoop::Clock::now() + std::chrono::nanoseconds(delay_ns));
}

uint64_t FlutterDesktopPluginRegistrar::AddFdWatch(
    int fd,
    uint32_t events,
    FlutterLinuxFdCallback callback,
    FlutterLinuxCallback destroy,
    void* user_data) {
  auto notify = std::make_shared<DestroyNotify>(destroy, user_data);
  uint64_t id = event_loop_->AddWatch(
      fd, ToEpollEvents(events),
      [notify = std::move(notify), fd, callback, user_data](uint32_t events) {
        callback(fd, FromEpollEvents(events), user_data);
      });
  if (id != 0) {
    watches_.insert(id);
  }
  return id;
}

bool FlutterDesktopPluginRegistrar::SetFdWatchEvents(uint64_t watch_id,
                                                     uint32_t events) {
  if (watches_.count(watch_id) == 0) {
    return false;
  }
  return event_loop_->SetWatchEvents(watch_id, ToEpollEvents(events));
}

void FlutterDesktopPluginRegistrar::RemoveFdWatch(uint64_t watch_id) {
  if (watches_.erase(watch_id) == 0) {
    return;
  }
  event_loop_->RemoveWatch(watch_id);
}

// The C ABI used by the client wrapper.

FlutterDesktopMessengerRef FlutterDesktopPluginRegistrarGetMessenger(
    FlutterDesktopPluginRegistrarRef registrar) {
  return registrar->messenger()->ToRef();
}

FlutterDesktopTextureRegistrarRef FlutterDesktopRegistrarGetTextureRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  // External textures are not yet supported.
  return nullptr;
}

void FlutterDesktopPluginRegistrarSetDestructionHandler(
    FlutterDesktopPluginRegistrarRef registrar,
    FlutterDesktopOnPluginRegistrarDestroyed callback) {
  registrar->SetDestructionHandler(callback);
}

int64_t FlutterDesktopTextureRegistrarRegisterExternalTexture(
    FlutterDesktopTextureRegistrarRef texture_registrar,
    const FlutterDesktopTextureInfo* info) {
  FML_LOG(ERROR) << "External textures are not yet supported";
  return -1;
}

void FlutterDesktopTextureRegistrarUnregisterExternalTexture(
    FlutterDesktopTextureRegistrarRef texture_registrar,
    int64_t texture_id,
    void (*callback)(void* user_data),
    void* user_data) {
  if (callback != nullptr) {
    callback(user_data);
  }
}

bool FlutterDesktopTextureRegistrarMarkExternalTextureFrameAvailable(
    FlutterDesktopTextureRegistrarRef texture_registrar,
    int64_t texture_id) {
  return false;
}
