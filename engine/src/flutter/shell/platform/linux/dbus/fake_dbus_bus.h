// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DBUS_FAKE_DBUS_BUS_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DBUS_FAKE_DBUS_BUS_H_

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "flutter/fml/unique_fd.h"
#include "flutter/shell/platform/linux/dbus/dbus_message.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

// The bus end of a connection.
class FakeBus {
 public:
  FakeBus() {
    int fds[2];
    EXPECT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds), 0);
    bus_fd_.reset(fds[0]);
    client_fd_.reset(fds[1]);
  }

  // Uses |bus_fd|, the bus end of an accepted connection.
  explicit FakeBus(fml::UniqueFD bus_fd) : bus_fd_(std::move(bus_fd)) {}

  fml::UniqueFD TakeClientFd() { return std::move(client_fd_); }

  // Reads until |count| bytes are available, returning false on timeout.
  bool Fill(size_t count) {
    while (input_.size() < count) {
      pollfd fd = {bus_fd_.get(), POLLIN, 0};
      if (poll(&fd, 1, 1000) != 1) {
        return false;
      }
      uint8_t buffer[4096];
      ssize_t result = read(bus_fd_.get(), buffer, sizeof(buffer));
      if (result <= 0) {
        return false;
      }
      input_.insert(input_.end(), buffer, buffer + result);
    }
    return true;
  }

  // Reads the authentication and replies OK.
  std::string Authenticate() {
    const std::string kEnd = "BEGIN\r\n";
    while (true) {
      std::string text(input_.begin(), input_.end());
      size_t end = text.find(kEnd);
      if (end != std::string::npos) {
        input_.erase(input_.begin(), input_.begin() + end + kEnd.size());
        Write("OK 1234deadbeef\r\n");
        return text.substr(0, end + kEnd.size());
      }
      if (!Fill(input_.size() + 1)) {
        return "";
      }
    }
  }

  std::optional<DBusMessage> ReadMessage() {
    if (!Fill(16)) {
      return std::nullopt;
    }
    size_t length = DBusMessage::GetLength(input_.data(), input_.size());
    if (length == 0 || !Fill(length)) {
      return std::nullopt;
    }
    std::optional<DBusMessage> message =
        DBusMessage::Parse(input_.data(), length);
    input_.erase(input_.begin(), input_.begin() + length);
    return message;
  }

  void Write(std::string_view text) {
    EXPECT_EQ(write(bus_fd_.get(), text.data(), text.size()),
              static_cast<ssize_t>(text.size()));
  }

  void Send(const DBusMessage& message) {
    std::vector<uint8_t> data = message.Serialize(serial_++);
    EXPECT_EQ(write(bus_fd_.get(), data.data(), data.size()),
              static_cast<ssize_t>(data.size()));
  }

  // Completes the connection setup.
  void Start() {
    EXPECT_NE(Authenticate().find("AUTH EXTERNAL "), std::string::npos);
    std::optional<DBusMessage> hello = ReadMessage();
    ASSERT_TRUE(hello.has_value());
    EXPECT_EQ(hello->member(), "Hello");
    DBusMessage reply = DBusMessage::MethodReturn(*hello);
    reply.writer().WriteString(":1.42");
    Send(reply);
  }

  void Close() { bus_fd_.reset(); }

  // True if the client has sent data that hasn't been read.
  bool HasInput() {
    if (!input_.empty()) {
      return true;
    }
    pollfd fd = {bus_fd_.get(), POLLIN, 0};
    return poll(&fd, 1, 0) == 1;
  }

 private:
  fml::UniqueFD bus_fd_;
  fml::UniqueFD client_fd_;
  std::vector<uint8_t> input_;
  uint32_t serial_ = 1;
};

// Runs |loop| until |condition| is true.
template <typename Condition>
bool RunUntil(EventLoop& loop, Condition condition) {
  auto deadline = EventLoop::Clock::now() + std::chrono::seconds(5);
  while (!condition()) {
    if (EventLoop::Clock::now() > deadline) {
      return false;
    }
    loop.PostTask([]() {},
                  EventLoop::Clock::now() + std::chrono::milliseconds(10));
    loop.Iterate(true);
  }
  return true;
}

}  // namespace testing
}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DBUS_FAKE_DBUS_BUS_H_
