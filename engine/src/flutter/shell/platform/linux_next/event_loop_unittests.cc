// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_next/event_loop.h"

#include <chrono>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

namespace flutter {
namespace testing {

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

}  // namespace testing
}  // namespace flutter
