// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/event_loop.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "flutter/fml/eintr_wrapper.h"
#include "flutter/fml/logging.h"

namespace flutter {

namespace {

constexpr int kMaxEvents = 16;

// Reads and discards the counter from an eventfd or timerfd.
void DrainFd(int fd) {
  uint64_t value;
  while (read(fd, &value, sizeof(value)) == sizeof(value)) {
  }
}

}  // namespace

EventLoop::EventLoop()
    : thread_id_(std::this_thread::get_id()),
      epoll_fd_(epoll_create1(EPOLL_CLOEXEC)),
      wake_fd_(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
      timer_fd_(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK)) {
  FML_CHECK(epoll_fd_.is_valid() && wake_fd_.is_valid() && timer_fd_.is_valid())
      << "Failed to create event loop file descriptors";

  epoll_event event = {};
  event.events = EPOLLIN;
  event.data.u64 = kWakeId;
  FML_CHECK(epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, wake_fd_.get(), &event) ==
            0);
  event.data.u64 = kTimerId;
  FML_CHECK(
      epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, timer_fd_.get(), &event) == 0);
}

EventLoop::~EventLoop() = default;

bool EventLoop::RunsTasksOnCurrentThread() const {
  return std::this_thread::get_id() == thread_id_;
}

void EventLoop::PostTask(Task task, TimePoint target_time) {
  bool is_earliest;
  {
    std::scoped_lock lock(tasks_mutex_);
    is_earliest = tasks_.empty() || target_time < tasks_.top().target_time;
    tasks_.push({target_time, next_task_order_++, std::move(task)});
  }

  // The loop only needs to recalculate its timeout if this task is due before
  // everything else.
  if (is_earliest && !RunsTasksOnCurrentThread()) {
    WakeUp();
  }
}

void EventLoop::AddSource(Source* source) {
  FML_DCHECK(RunsTasksOnCurrentThread());
  uint64_t id = next_id_++;
  epoll_event event = {};
  event.events = EPOLLIN;
  event.data.u64 = id;
  if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, source->GetFd(), &event) != 0) {
    FML_LOG(ERROR) << "Failed to add event loop source: " << strerror(errno);
    return;
  }
  sources_.emplace_back(id, source);
}

void EventLoop::RemoveSource(Source* source) {
  FML_DCHECK(RunsTasksOnCurrentThread());
  auto it =
      std::find_if(sources_.begin(), sources_.end(),
                   [source](const auto& s) { return s.second == source; });
  if (it == sources_.end()) {
    return;
  }
  sources_.erase(it);
  epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, source->GetFd(), nullptr);
}

uint64_t EventLoop::AddWatch(int fd, uint32_t events, WatchCallback callback) {
  FML_DCHECK(RunsTasksOnCurrentThread());
  uint64_t id = next_id_++;
  epoll_event event = {};
  event.events = events;
  event.data.u64 = id;
  if (epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, fd, &event) != 0) {
    FML_LOG(ERROR) << "Failed to watch file descriptor: " << strerror(errno);
    return 0;
  }
  watches_[id] = {fd, std::make_shared<WatchCallback>(std::move(callback))};
  return id;
}

bool EventLoop::SetWatchEvents(uint64_t id, uint32_t events) {
  FML_DCHECK(RunsTasksOnCurrentThread());
  auto it = watches_.find(id);
  if (it == watches_.end()) {
    return false;
  }
  epoll_event event = {};
  event.events = events;
  event.data.u64 = id;
  return epoll_ctl(epoll_fd_.get(), EPOLL_CTL_MOD, it->second.fd, &event) == 0;
}

void EventLoop::RemoveWatch(uint64_t id) {
  FML_DCHECK(RunsTasksOnCurrentThread());
  auto it = watches_.find(id);
  if (it == watches_.end()) {
    return;
  }
  epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, it->second.fd, nullptr);
  watches_.erase(it);
}

void EventLoop::Run() {
  FML_DCHECK(RunsTasksOnCurrentThread());
  while (!quit_) {
    Iterate(true);
  }
  quit_ = false;
}

void EventLoop::Quit() {
  quit_ = true;
  WakeUp();
}

void EventLoop::Iterate(bool may_block) {
  FML_DCHECK(RunsTasksOnCurrentThread());

  // Copy the sources so they can be removed while dispatching.
  std::vector<std::pair<uint64_t, Source*>> sources = sources_;
  for (const auto& [id, source] : sources) {
    source->Prepare();
  }

  int timeout = 0;
  if (may_block && !quit_) {
    TimePoint next_task_time;
    {
      std::scoped_lock lock(tasks_mutex_);
      next_task_time =
          tasks_.empty() ? TimePoint::max() : tasks_.top().target_time;
    }
    if (next_task_time > Clock::now()) {
      // Use a timer rather than the epoll timeout, as that only has
      // millisecond precision and task timing (e.g. vsync) needs better.
      ArmTimer(next_task_time);
      timeout = -1;
    }
  }

  epoll_event events[kMaxEvents];
  int n_events = FML_HANDLE_EINTR(
      epoll_wait(epoll_fd_.get(), events, kMaxEvents, timeout));
  if (n_events < 0) {
    FML_LOG(ERROR) << "Failed to wait for events: " << strerror(errno);
    n_events = 0;
  }

  std::map<uint64_t, uint32_t> ready;
  for (int i = 0; i < n_events; i++) {
    uint64_t id = events[i].data.u64;
    if (id == kWakeId) {
      DrainFd(wake_fd_.get());
    } else if (id == kTimerId) {
      DrainFd(timer_fd_.get());
    } else {
      ready[id] = events[i].events;
    }
  }

  for (const auto& [id, source] : sources) {
    // Skip any source removed while dispatching an earlier source.
    if (std::find(sources_.begin(), sources_.end(),
                  std::make_pair(id, source)) == sources_.end()) {
      continue;
    }
    source->Dispatch(ready.count(id) != 0);
  }

  for (const auto& [id, ready_events] : ready) {
    // Skip any watch removed by an earlier callback. The callback is kept
    // alive in case it removes its own watch.
    auto it = watches_.find(id);
    if (it == watches_.end()) {
      continue;
    }
    std::shared_ptr<WatchCallback> callback = it->second.callback;
    (*callback)(ready_events);
  }

  RunExpiredTasks();
}

void EventLoop::WakeUp() {
  uint64_t value = 1;
  if (write(wake_fd_.get(), &value, sizeof(value)) < 0 && errno != EAGAIN) {
    FML_LOG(ERROR) << "Failed to wake event loop: " << strerror(errno);
  }
}

void EventLoop::ArmTimer(TimePoint time) {
  if (time == timer_time_) {
    return;
  }
  timer_time_ = time;

  itimerspec spec = {};
  if (time != TimePoint::max()) {
    // steady_clock is CLOCK_MONOTONIC on Linux, so the time since its epoch is
    // the absolute timer value.
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  time.time_since_epoch())
                  .count();
    // A zero value disarms the timer, so ensure it is always set.
    ns = std::max<int64_t>(ns, 1);
    spec.it_value.tv_sec = ns / 1000000000;
    spec.it_value.tv_nsec = ns % 1000000000;
  }
  if (timerfd_settime(timer_fd_.get(), TFD_TIMER_ABSTIME, &spec, nullptr) !=
      0) {
    FML_LOG(ERROR) << "Failed to set event loop timer: " << strerror(errno);
  }
}

void EventLoop::RunExpiredTasks() {
  std::vector<Task> expired_tasks;
  {
    std::scoped_lock lock(tasks_mutex_);
    TimePoint now = Clock::now();
    while (!tasks_.empty() && tasks_.top().target_time <= now) {
      // The queue only exposes a const reference, but the task is about to be
      // removed so it is safe to move from it.
      expired_tasks.push_back(
          std::move(const_cast<ScheduledTask&>(tasks_.top()).task));
      tasks_.pop();
    }
  }

  for (Task& task : expired_tasks) {
    task();
  }
}

}  // namespace flutter
