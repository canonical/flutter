// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/dbus/dbus_connection.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "flutter/shell/platform/linux/dbus/fake_dbus_bus.h"
#include "gtest/gtest.h"

namespace flutter {
namespace testing {

TEST(DBusConnectionTest, Ready) {
  EventLoop loop;
  FakeBus bus;
  DBusConnection connection(&loop, bus.TakeClientFd());
  bool ready = false;
  connection.set_ready_callback([&ready]() { ready = true; });
  bus.Start();
  EXPECT_TRUE(RunUntil(loop, [&ready]() { return ready; }));
  EXPECT_EQ(connection.unique_name(), ":1.42");
}

TEST(DBusConnectionTest, AuthenticationRejected) {
  EventLoop loop;
  FakeBus bus;
  DBusConnection connection(&loop, bus.TakeClientFd());
  bool disconnected = false;
  connection.set_disconnected_callback(
      [&disconnected]() { disconnected = true; });
  bus.Write("REJECTED EXTERNAL\r\n");
  EXPECT_TRUE(RunUntil(loop, [&disconnected]() { return disconnected; }));
  EXPECT_FALSE(connection.is_connected());
}

TEST(DBusConnectionTest, CallAndReply) {
  EventLoop loop;
  FakeBus bus;
  DBusConnection connection(&loop, bus.TakeClientFd());
  bus.Start();

  DBusMessage call =
      DBusMessage::MethodCall("org.example", "/", "org.example", "Add");
  call.writer().WriteInt32(1);
  call.writer().WriteInt32(2);
  int32_t result = 0;
  connection.Call(call, [&result](const DBusMessage* reply) {
    ASSERT_NE(reply, nullptr);
    EXPECT_TRUE(reply->reader().ReadInt32(&result));
  });

  std::optional<DBusMessage> received = bus.ReadMessage();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->member(), "Add");
  DBusReader reader = received->reader();
  int32_t a, b;
  EXPECT_TRUE(reader.ReadInt32(&a));
  EXPECT_TRUE(reader.ReadInt32(&b));
  DBusMessage reply = DBusMessage::MethodReturn(*received);
  reply.writer().WriteInt32(a + b);
  bus.Send(reply);

  EXPECT_TRUE(RunUntil(loop, [&result]() { return result != 0; }));
  EXPECT_EQ(result, 3);
}

TEST(DBusConnectionTest, CallTimeout) {
  EventLoop loop;
  FakeBus bus;
  DBusConnection connection(&loop, bus.TakeClientFd());
  bus.Start();
  bool called = false;
  connection.Call(
      DBusMessage::MethodCall("org.example", "/", "org.example", "Slow"),
      [&called](const DBusMessage* reply) {
        EXPECT_EQ(reply, nullptr);
        called = true;
      },
      std::chrono::milliseconds(20));
  EXPECT_TRUE(RunUntil(loop, [&called]() { return called; }));
}

TEST(DBusConnectionTest, DisconnectFailsCalls) {
  EventLoop loop;
  FakeBus bus;
  DBusConnection connection(&loop, bus.TakeClientFd());
  bus.Start();
  bool called = false;
  bool disconnected = false;
  connection.set_disconnected_callback(
      [&disconnected]() { disconnected = true; });
  connection.Call(
      DBusMessage::MethodCall("org.example", "/", "org.example", "Never"),
      [&called](const DBusMessage* reply) {
        EXPECT_EQ(reply, nullptr);
        called = true;
      });
  bus.Close();
  EXPECT_TRUE(RunUntil(loop, [&disconnected]() { return disconnected; }));
  EXPECT_TRUE(called);
  EXPECT_FALSE(connection.is_connected());
}

TEST(DBusConnectionTest, MethodHandler) {
  EventLoop loop;
  FakeBus bus;
  DBusConnection connection(&loop, bus.TakeClientFd());
  connection.set_method_handler([&connection](const DBusMessage& call) {
    if (call.member() != "Known") {
      return false;
    }
    DBusMessage reply = DBusMessage::MethodReturn(call);
    reply.writer().WriteString("yes");
    connection.Send(reply);
    return true;
  });
  bus.Start();
  // Skip past the Hello reply processing.
  RunUntil(loop, [&connection]() { return connection.is_ready(); });

  DBusMessage known =
      DBusMessage::MethodCall(":1.42", "/obj", "org.example", "Known");
  bus.Send(known);
  DBusMessage unknown =
      DBusMessage::MethodCall(":1.42", "/obj", "org.example", "Unknown");
  bus.Send(unknown);
  DBusMessage ping = DBusMessage::MethodCall(
      ":1.42", "/", "org.freedesktop.DBus.Peer", "Ping");
  bus.Send(ping);

  bool done = false;
  loop.PostTask([&done]() { done = true; },
                EventLoop::Clock::now() + std::chrono::milliseconds(50));
  RunUntil(loop, [&done]() { return done; });

  std::optional<DBusMessage> reply = bus.ReadMessage();
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->type(), DBusMessage::Type::kMethodReturn);
  std::string text;
  EXPECT_TRUE(reply->reader().ReadString(&text));
  EXPECT_EQ(text, "yes");

  reply = bus.ReadMessage();
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->type(), DBusMessage::Type::kError);
  EXPECT_EQ(reply->error_name(), "org.freedesktop.DBus.Error.UnknownMethod");

  reply = bus.ReadMessage();
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->type(), DBusMessage::Type::kMethodReturn);
}

TEST(DBusConnectionTest, Signal) {
  EventLoop loop;
  FakeBus bus;
  DBusConnection connection(&loop, bus.TakeClientFd());
  std::string member;
  connection.set_signal_handler(
      [&member](const DBusMessage& signal) { member = signal.member(); });
  bus.Start();
  bus.Send(DBusMessage::Signal("/obj", "org.example", "Changed"));
  EXPECT_TRUE(RunUntil(loop, [&member]() { return !member.empty(); }));
  EXPECT_EQ(member, "Changed");
}

TEST(DBusConnectionTest, DestroyInCallback) {
  EventLoop loop;
  FakeBus bus;
  auto connection = std::make_unique<DBusConnection>(&loop, bus.TakeClientFd());
  connection->set_signal_handler(
      [&connection](const DBusMessage& signal) { connection.reset(); });
  bus.Start();
  bus.Send(DBusMessage::Signal("/obj", "org.example", "A"));
  bus.Send(DBusMessage::Signal("/obj", "org.example", "B"));
  EXPECT_TRUE(RunUntil(loop, [&connection]() { return !connection; }));
}

TEST(DBusConnectionTest, ConnectAddress) {
  EventLoop loop;
  char dir_template[] = "/tmp/dbus-test-XXXXXX";
  ASSERT_NE(mkdtemp(dir_template), nullptr);
  std::string path = std::string(dir_template) + "/bus";

  fml::UniqueFD server(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
  ASSERT_EQ(
      bind(server.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
  ASSERT_EQ(listen(server.get(), 1), 0);

  EXPECT_EQ(DBusConnection::Connect(&loop, "tcp:host=localhost,port=1"),
            nullptr);
  // The first address fails, so the second is used. '-' is escaped.
  std::string escaped;
  for (char c : path) {
    if (c == '-') {
      escaped += "%2d";
    } else {
      escaped += c;
    }
  }
  std::unique_ptr<DBusConnection> connection = DBusConnection::Connect(
      &loop, "unix:path=/nonexistent/bus;unix:path=" + escaped + ",guid=1");
  EXPECT_NE(connection, nullptr);

  connection.reset();
  unlink(path.c_str());
  rmdir(dir_template);
}

}  // namespace testing
}  // namespace flutter
