// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/event_loop.h"

#include <sys/epoll.h>
#include <unistd.h>

#include <thread>
#include <vector>

#include "flutter/fml/unique_fd.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

// A source backed by a pipe.
class PipeSource : public EventLoop::Source {
 public:
  PipeSource() {
    int fds[2];
    EXPECT_EQ(pipe(fds), 0);
    read_fd_.reset(fds[0]);
    write_fd_.reset(fds[1]);
  }

  int GetFd() const override { return read_fd_.get(); }

  void Prepare() override { prepare_count++; }

  void Dispatch(bool readable) override {
    if (readable) {
      char c;
      EXPECT_EQ(read(read_fd_.get(), &c, 1), 1);
      read_count++;
    }
  }

  void Write() { EXPECT_EQ(write(write_fd_.get(), "x", 1), 1); }

  int prepare_count = 0;
  int read_count = 0;

 private:
  fml::UniqueFD read_fd_;
  fml::UniqueFD write_fd_;
};

}  // namespace

TEST(EventLoopTest, RunsTasksOnCurrentThread) {
  EventLoop loop;
  EXPECT_TRUE(loop.RunsTasksOnCurrentThread());

  bool other_thread_result = true;
  std::thread thread(
      [&]() { other_thread_result = loop.RunsTasksOnCurrentThread(); });
  thread.join();
  EXPECT_FALSE(other_thread_result);
}

TEST(EventLoopTest, RunsTasksInOrder) {
  EventLoop loop;
  std::vector<int> order;
  loop.PostTask([&]() { order.push_back(1); });
  loop.PostTask([&]() { order.push_back(2); });
  loop.PostTask([&]() {
    order.push_back(3);
    loop.Quit();
  });
  loop.Run();
  EXPECT_EQ(order, std::vector<int>({1, 2, 3}));
}

TEST(EventLoopTest, RunsDelayedTasksByTargetTime) {
  EventLoop loop;
  std::vector<int> order;
  auto now = EventLoop::Clock::now();
  loop.PostTask(
      [&]() {
        order.push_back(2);
        loop.Quit();
      },
      now + std::chrono::milliseconds(20));
  loop.PostTask([&]() { order.push_back(1); },
                now + std::chrono::milliseconds(10));
  loop.Run();
  EXPECT_EQ(order, std::vector<int>({1, 2}));
  EXPECT_GE(EventLoop::Clock::now() - now, std::chrono::milliseconds(20));
}

TEST(EventLoopTest, PostTaskFromOtherThreadWakesLoop) {
  EventLoop loop;
  bool ran = false;
  std::thread thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    loop.PostTask([&]() {
      ran = true;
      loop.Quit();
    });
  });
  loop.Run();
  thread.join();
  EXPECT_TRUE(ran);
}

TEST(EventLoopTest, QuitFromOtherThread) {
  EventLoop loop;
  std::thread thread([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    loop.Quit();
  });
  loop.Run();
  thread.join();
}

TEST(EventLoopTest, QuitBeforeRun) {
  EventLoop loop;
  loop.Quit();
  loop.Run();

  // The loop can be run again afterwards.
  bool ran = false;
  loop.PostTask([&]() {
    ran = true;
    loop.Quit();
  });
  loop.Run();
  EXPECT_TRUE(ran);
}

TEST(EventLoopTest, DispatchesSources) {
  EventLoop loop;
  PipeSource source;
  loop.AddSource(&source);

  loop.Iterate(false);
  EXPECT_EQ(source.prepare_count, 1);
  EXPECT_EQ(source.read_count, 0);

  source.Write();
  loop.Iterate(true);
  EXPECT_EQ(source.prepare_count, 2);
  EXPECT_EQ(source.read_count, 1);

  loop.RemoveSource(&source);
  source.Write();
  loop.Iterate(false);
  EXPECT_EQ(source.prepare_count, 2);
  EXPECT_EQ(source.read_count, 1);
}

TEST(EventLoopTest, WatchesFileDescriptors) {
  EventLoop loop;
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  fml::UniqueFD read_fd(fds[0]);
  fml::UniqueFD write_fd(fds[1]);

  std::vector<uint32_t> read_events;
  uint64_t read_id =
      loop.AddWatch(read_fd.get(), EPOLLIN, [&](uint32_t events) {
        char c;
        EXPECT_EQ(read(read_fd.get(), &c, 1), 1);
        read_events.push_back(events);
      });
  EXPECT_NE(read_id, 0u);

  loop.Iterate(false);
  EXPECT_TRUE(read_events.empty());

  EXPECT_EQ(write(write_fd.get(), "x", 1), 1);
  loop.Iterate(true);
  ASSERT_EQ(read_events.size(), 1u);
  EXPECT_EQ(read_events[0], static_cast<uint32_t>(EPOLLIN));

  // A pipe can always be written, once asked for.
  int write_count = 0;
  uint64_t write_id =
      loop.AddWatch(write_fd.get(), 0, [&](uint32_t events) { write_count++; });
  loop.Iterate(false);
  EXPECT_EQ(write_count, 0);
  EXPECT_TRUE(loop.SetWatchEvents(write_id, EPOLLOUT));
  loop.Iterate(false);
  EXPECT_EQ(write_count, 1);
  loop.RemoveWatch(write_id);
  loop.Iterate(false);
  EXPECT_EQ(write_count, 1);

  loop.RemoveWatch(read_id);
  EXPECT_EQ(write(write_fd.get(), "x", 1), 1);
  loop.Iterate(false);
  EXPECT_EQ(read_events.size(), 1u);
}

TEST(EventLoopTest, WatchCanRemoveItself) {
  EventLoop loop;
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  fml::UniqueFD read_fd(fds[0]);
  fml::UniqueFD write_fd(fds[1]);

  // The callback state is destroyed with the watch.
  auto state = std::make_shared<int>(0);
  std::weak_ptr<int> weak_state = state;
  uint64_t id = 0;
  id = loop.AddWatch(write_fd.get(), EPOLLOUT,
                     [&loop, &id, state](uint32_t events) {
                       (*state)++;
                       loop.RemoveWatch(id);
                       // Still valid while running.
                       (*state)++;
                     });
  state.reset();
  loop.Iterate(false);
  loop.Iterate(false);
  EXPECT_TRUE(weak_state.expired());
}

}  // namespace testing
}  // namespace flutter
