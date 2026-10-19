// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_SCOPED_EVENT_LOOP_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_SCOPED_EVENT_LOOP_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <set>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/event_loop.h"

namespace flutter {

// Access to an event loop whose tasks and watches stop when this is
// destroyed. Used to give plugins access to the event loop for the lifetime
// of their engine.
//
// PostTask may be called from any thread while this exists. Everything else
// must be called on the loop thread.
class ScopedEventLoop {
 public:
  // |event_loop| must outlive this.
  explicit ScopedEventLoop(EventLoop* event_loop);

  // Removes all watches. Tasks that have yet to run are destroyed without
  // being run.
  ~ScopedEventLoop();

  // Runs |task| on the loop thread after |delay|.
  void PostTask(EventLoop::Task task, std::chrono::nanoseconds delay);

  // See EventLoop::AddWatch.
  uint64_t AddWatch(int fd, uint32_t events, EventLoop::WatchCallback callback);

  // See EventLoop::SetWatchEvents. Returns false if the watch wasn't added
  // with this.
  bool SetWatchEvents(uint64_t id, uint32_t events);

  // See EventLoop::RemoveWatch. Ignores watches not added with this.
  void RemoveWatch(uint64_t id);

 private:
  EventLoop* event_loop_;

  // Tasks hold a weak reference to this, so they don't run after this is
  // destroyed.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  std::set<uint64_t> watches_;

  FML_DISALLOW_COPY_AND_ASSIGN(ScopedEventLoop);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_SCOPED_EVENT_LOOP_H_
