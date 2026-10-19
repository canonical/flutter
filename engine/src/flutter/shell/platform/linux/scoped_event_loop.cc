// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/scoped_event_loop.h"

#include <algorithm>
#include <utility>

namespace flutter {

ScopedEventLoop::ScopedEventLoop(EventLoop* event_loop)
    : event_loop_(event_loop) {}

ScopedEventLoop::~ScopedEventLoop() {
  for (uint64_t id : watches_) {
    event_loop_->RemoveWatch(id);
  }
}

void ScopedEventLoop::PostTask(EventLoop::Task task,
                               std::chrono::nanoseconds delay) {
  // The task is destroyed (along with anything it holds) on the loop thread
  // when it is due, whether or not it runs.
  event_loop_->PostTask(
      [alive = std::weak_ptr<bool>(alive_), task = std::move(task)]() {
        if (!alive.expired()) {
          task();
        }
      },
      EventLoop::Clock::now() +
          std::max(delay, std::chrono::nanoseconds::zero()));
}

uint64_t ScopedEventLoop::AddWatch(int fd,
                                   uint32_t events,
                                   EventLoop::WatchCallback callback) {
  uint64_t id = event_loop_->AddWatch(fd, events, std::move(callback));
  if (id != 0) {
    watches_.insert(id);
  }
  return id;
}

bool ScopedEventLoop::SetWatchEvents(uint64_t id, uint32_t events) {
  if (watches_.count(id) == 0) {
    return false;
  }
  return event_loop_->SetWatchEvents(id, events);
}

void ScopedEventLoop::RemoveWatch(uint64_t id) {
  if (watches_.erase(id) != 0) {
    event_loop_->RemoveWatch(id);
  }
}

}  // namespace flutter
