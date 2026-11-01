// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DBUS_DBUS_CONNECTION_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DBUS_DBUS_CONNECTION_H_

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/fml/unique_fd.h"
#include "flutter/shell/platform/linux/dbus/dbus_message.h"
#include "flutter/shell/platform/linux/event_loop.h"

namespace flutter {

// A connection to a D-Bus message bus, driven by an EventLoop.
//
// All methods must be called on the loop thread. Callbacks may destroy the
// connection.
class DBusConnection {
 public:
  // Called with the reply to a method call, which is either a method return
  // or an error. |reply| is nullptr if the call timed out or the connection
  // was lost.
  using ReplyCallback = std::function<void(const DBusMessage* reply)>;

  // Called with a method call to this connection. Returns false if the
  // method isn't known.
  using MethodHandler = std::function<bool(const DBusMessage& call)>;

  using SignalHandler = std::function<void(const DBusMessage& signal)>;

  static constexpr std::chrono::milliseconds kDefaultTimeout =
      std::chrono::seconds(25);

  // Returns the address of the session bus.
  static std::string SessionBusAddress();

  // Returns the address of the system bus.
  static std::string SystemBusAddress();

  // Connects to the bus at |address|, a list of D-Bus server addresses.
  // Returns nullptr if none can be connected to. The connection is ready to
  // use, though messages are only exchanged once authentication completes.
  //
  // If |unix_fds| is true, the connection asks to receive file descriptors
  // attached to messages.
  static std::unique_ptr<DBusConnection> Connect(EventLoop* loop,
                                                 std::string_view address,
                                                 bool unix_fds = false);

  // Uses an already connected |socket|.
  DBusConnection(EventLoop* loop, fml::UniqueFD socket, bool unix_fds = false);
  ~DBusConnection();

  // True once the bus has assigned this connection a unique name.
  bool is_ready() const { return !unique_name_.empty(); }
  const std::string& unique_name() const { return unique_name_; }
  bool is_connected() const { return socket_.is_valid(); }

  // Called once the connection is ready.
  void set_ready_callback(std::function<void()> callback) {
    ready_callback_ = std::move(callback);
  }

  // Called if the connection is lost.
  void set_disconnected_callback(std::function<void()> callback) {
    disconnected_callback_ = std::move(callback);
  }

  // Handles method calls to this connection. Unhandled calls get an
  // UnknownMethod error.
  void set_method_handler(MethodHandler handler) {
    method_handler_ = std::move(handler);
  }

  // Handles signals received by this connection.
  void set_signal_handler(SignalHandler handler) {
    signal_handler_ = std::move(handler);
  }

  // Sends |message| without waiting for a reply.
  void Send(const DBusMessage& message);

  // Sends the method call |message| and calls |callback| with the reply.
  void Call(const DBusMessage& message,
            ReplyCallback callback,
            std::chrono::milliseconds timeout = kDefaultTimeout);

  // Sends the method call |message| and waits for the reply, which is either
  // a method return or an error. Returns nullopt if the call timed out or
  // the connection was lost. Messages received while waiting are dispatched
  // afterwards, so this may be called from callbacks.
  std::optional<DBusMessage> CallSync(
      const DBusMessage& message,
      std::chrono::milliseconds timeout = kDefaultTimeout);

  // True if file descriptors can be received on the connection. Only known
  // once authentication completes.
  bool unix_fds_enabled() const { return unix_fds_enabled_; }

  // Subscribes to signals matching |rule| from other connections.
  void AddMatch(std::string_view rule);

 private:
  struct PendingCall {
    ReplyCallback callback;
  };

  uint32_t NextSerial();
  void Write(std::vector<uint8_t> data);
  void Flush();
  void OnEvents(uint32_t events);
  // Returns false if the connection was destroyed or lost.
  bool ProcessInput();
  // Reads the available data and file descriptors. Returns false if the
  // connection closed or failed.
  bool ReadSocket();
  // Moves complete messages from the input into |incoming_|. Returns false
  // if the input is invalid.
  bool ParseInput();
  bool ProcessAuthentication();
  // Dispatches the messages in |incoming_|. Returns false if the connection
  // was destroyed or lost.
  bool DispatchIncoming();
  // Dispatches |incoming_| from the loop, for messages received while
  // waiting for a reply.
  void ScheduleDispatch();
  bool Dispatch(const DBusMessage& message);
  void OnTimeout(uint32_t serial);
  void Disconnect();

  EventLoop* loop_;
  fml::UniqueFD socket_;
  uint64_t watch_ = 0;
  bool authenticated_ = false;
  bool negotiate_unix_fds_ = false;
  bool unix_fds_enabled_ = false;
  // True once the server has accepted authentication, while waiting for the
  // reply to the file descriptor negotiation.
  bool auth_accepted_ = false;
  bool dispatch_scheduled_ = false;
  bool write_failed_ = false;
  uint32_t next_serial_ = 1;
  std::string unique_name_;
  std::vector<uint8_t> input_;
  // File descriptors received that haven't been attached to a message.
  std::deque<fml::UniqueFD> received_fds_;
  // Messages received that haven't been dispatched.
  std::deque<DBusMessage> incoming_;
  std::vector<uint8_t> output_;
  std::map<uint32_t, PendingCall> pending_calls_;
  std::function<void()> ready_callback_;
  std::function<void()> disconnected_callback_;
  MethodHandler method_handler_;
  SignalHandler signal_handler_;

  // Reset on destruction, so callbacks and tasks can detect it.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(DBusConnection);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DBUS_DBUS_CONNECTION_H_
