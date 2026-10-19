// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/scoped_event_loop.h"

#include <sys/epoll.h>
#include <unistd.h>

#include <memory>
#include <thread>

#include "flutter/fml/unique_fd.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

// Sets a flag when destroyed.
class DestroyFlag {
 public:
  explicit DestroyFlag(bool* destroyed) : destroyed_(destroyed) {}
  ~DestroyFlag() { *destroyed_ = true; }

 private:
  bool* destroyed_;
};

}  // namespace

TEST(ScopedEventLoopTest, RunsTasks) {
  EventLoop loop;
  ScopedEventLoop scoped_loop(&loop);
  int count = 0;
  scoped_loop.PostTask([&count]() { count++; }, {});
  loop.Iterate(false);
  EXPECT_EQ(count, 1);
}

TEST(ScopedEventLoopTest, PostTaskFromOtherThread) {
  EventLoop loop;
  ScopedEventLoop scoped_loop(&loop);
  bool ran = false;
  std::thread thread(
      [&]() { scoped_loop.PostTask([&ran]() { ran = true; }, {}); });
  thread.join();
  loop.Iterate(true);
  EXPECT_TRUE(ran);
}

TEST(ScopedEventLoopTest, DelayedTask) {
  EventLoop loop;
  ScopedEventLoop scoped_loop(&loop);
  bool ran = false;
  scoped_loop.PostTask([&ran]() { ran = true; }, std::chrono::hours(1));
  loop.Iterate(false);
  EXPECT_FALSE(ran);
}

TEST(ScopedEventLoopTest, DestroysTasksWithoutRunning) {
  EventLoop loop;
  auto scoped_loop = std::make_unique<ScopedEventLoop>(&loop);
  bool ran = false;
  bool destroyed = false;
  auto flag = std::make_shared<DestroyFlag>(&destroyed);
  scoped_loop->PostTask([&ran, flag = std::move(flag)]() { ran = true; }, {});
  scoped_loop.reset();
  EXPECT_FALSE(destroyed);
  loop.Iterate(false);
  EXPECT_FALSE(ran);
  EXPECT_TRUE(destroyed);
}

TEST(ScopedEventLoopTest, RemovesWatchesWhenDestroyed) {
  EventLoop loop;
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  fml::UniqueFD read_fd(fds[0]);
  fml::UniqueFD write_fd(fds[1]);

  auto scoped_loop = std::make_unique<ScopedEventLoop>(&loop);
  int count = 0;
  bool destroyed = false;
  auto flag = std::make_shared<DestroyFlag>(&destroyed);
  uint64_t id = scoped_loop->AddWatch(
      read_fd.get(), EPOLLIN,
      [&count, flag = std::move(flag)](uint32_t events) { count++; });
  ASSERT_NE(id, 0u);

  ASSERT_EQ(write(write_fd.get(), "x", 1), 1);
  loop.Iterate(false);
  EXPECT_EQ(count, 1);

  scoped_loop.reset();
  loop.Iterate(false);
  EXPECT_EQ(count, 1);
  EXPECT_TRUE(destroyed);
}

TEST(ScopedEventLoopTest, IgnoresOtherWatches) {
  EventLoop loop;
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  fml::UniqueFD read_fd(fds[0]);
  fml::UniqueFD write_fd(fds[1]);

  int count = 0;
  uint64_t id =
      loop.AddWatch(read_fd.get(), EPOLLIN, [&count](uint32_t) { count++; });
  {
    ScopedEventLoop scoped_loop(&loop);
    EXPECT_FALSE(scoped_loop.SetWatchEvents(id, EPOLLIN | EPOLLOUT));
    scoped_loop.RemoveWatch(id);
  }

  ASSERT_EQ(write(write_fd.get(), "x", 1), 1);
  loop.Iterate(false);
  EXPECT_EQ(count, 1);
  loop.RemoveWatch(id);
}

}  // namespace testing
}  // namespace flutter
