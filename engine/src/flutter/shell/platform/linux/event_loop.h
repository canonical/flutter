// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_EVENT_LOOP_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_EVENT_LOOP_H_

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/fml/unique_fd.h"

namespace flutter {

// An event loop that runs on a single thread and processes file descriptor
// events (e.g. from the Wayland connection) and scheduled tasks.
//
// Tasks can be posted from any thread. Everything else must be called on the
// thread that created the loop.
class EventLoop {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using Task = std::function<void()>;

  // A file descriptor that the loop waits on.
  class Source {
   public:
    virtual ~Source() = default;

    // The file descriptor to wait for input on.
    virtual int GetFd() const = 0;

    // Called before the loop waits.
    virtual void Prepare() {}

    // Called after the loop has waited. |readable| is true if input is
    // available on the file descriptor.
    virtual void Dispatch(bool readable) = 0;
  };

  EventLoop();
  ~EventLoop();

  // Returns true if called on the thread that runs this loop.
  bool RunsTasksOnCurrentThread() const;

  // Schedules |task| to run on the loop thread no earlier than |target_time|.
  // Tasks with the same target time run in the order they were posted.
  //
  // May be called from any thread.
  void PostTask(Task task, TimePoint target_time = TimePoint::min());

  // Adds a source for the loop to wait on. The source must outlive the loop
  // or be removed with RemoveSource.
  void AddSource(Source* source);

  // Stops waiting on |source|.
  void RemoveSource(Source* source);

  // Runs the loop until Quit is called. If Quit was called before Run, this
  // returns immediately.
  void Run();

  // Causes Run to return after the current iteration.
  //
  // May be called from any thread.
  void Quit();

  // Runs a single iteration of the loop. If |may_block| is true, this waits
  // until there is a source event or a task is due.
  void Iterate(bool may_block);

 private:
  struct ScheduledTask {
    TimePoint target_time;
    uint64_t order;
    Task task;

    // Orders tasks so the earliest is at the top of the priority queue.
    struct Later {
      bool operator()(const ScheduledTask& a, const ScheduledTask& b) const {
        if (a.target_time == b.target_time) {
          return a.order > b.order;
        }
        return a.target_time > b.target_time;
      }
    };
  };

  // Wakes the loop if it is waiting.
  void WakeUp();

  // Sets the timer to expire at |time|, or disarms it if |time| is max().
  void ArmTimer(TimePoint time);

  // Runs all tasks that are due.
  void RunExpiredTasks();

  std::thread::id thread_id_;
  fml::UniqueFD epoll_fd_;
  fml::UniqueFD wake_fd_;
  fml::UniqueFD timer_fd_;
  TimePoint timer_time_ = TimePoint::max();
  std::vector<Source*> sources_;
  std::atomic<bool> quit_ = false;

  std::mutex tasks_mutex_;
  std::priority_queue<ScheduledTask,
                      std::deque<ScheduledTask>,
                      ScheduledTask::Later>
      tasks_;
  uint64_t next_task_order_ = 0;

  FML_DISALLOW_COPY_AND_ASSIGN(EventLoop);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_EVENT_LOOP_H_
