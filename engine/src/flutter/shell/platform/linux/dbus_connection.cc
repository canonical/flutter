// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/dbus_connection.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "flutter/fml/logging.h"

namespace flutter {

namespace {

constexpr char kBusName[] = "org.freedesktop.DBus";
constexpr char kBusPath[] = "/org/freedesktop/DBus";
constexpr char kBusInterface[] = "org.freedesktop.DBus";
constexpr char kPeerInterface[] = "org.freedesktop.DBus.Peer";

// Splits |value| at each |separator|.
std::vector<std::string_view> Split(std::string_view value, char separator) {
  std::vector<std::string_view> parts;
  while (true) {
    size_t end = value.find(separator);
    parts.push_back(value.substr(0, end));
    if (end == std::string_view::npos) {
      return parts;
    }
    value.remove_prefix(end + 1);
  }
}

int HexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

// Decodes the %-escapes in a D-Bus address value.
bool Unescape(std::string_view value, std::string* result) {
  result->clear();
  for (size_t i = 0; i < value.size(); i++) {
    if (value[i] != '%') {
      result->push_back(value[i]);
      continue;
    }
    if (i + 2 >= value.size()) {
      return false;
    }
    int high = HexValue(value[i + 1]);
    int low = HexValue(value[i + 2]);
    if (high < 0 || low < 0) {
      return false;
    }
    result->push_back(static_cast<char>(high * 16 + low));
    i += 2;
  }
  return true;
}

// Connects to a single "unix:" address, returning an invalid fd on failure.
fml::UniqueFD ConnectUnix(std::string_view address) {
  constexpr std::string_view kUnixPrefix = "unix:";
  if (address.substr(0, kUnixPrefix.size()) != kUnixPrefix) {
    return {};
  }
  std::string path;
  bool abstract = false;
  for (std::string_view pair : Split(address.substr(kUnixPrefix.size()), ',')) {
    size_t equals = pair.find('=');
    if (equals == std::string_view::npos) {
      continue;
    }
    std::string_view key = pair.substr(0, equals);
    if (key == "path" || key == "abstract") {
      if (!Unescape(pair.substr(equals + 1), &path)) {
        return {};
      }
      abstract = key == "abstract";
    }
  }

  sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  size_t offset = abstract ? 1 : 0;
  if (path.empty() || path.size() + offset >= sizeof(addr.sun_path)) {
    return {};
  }
  std::memcpy(addr.sun_path + offset, path.data(), path.size());
  socklen_t length = offsetof(sockaddr_un, sun_path) + offset + path.size() +
                     (abstract ? 0 : 1);

  fml::UniqueFD fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  if (!fd.is_valid() ||
      connect(fd.get(), reinterpret_cast<sockaddr*>(&addr), length) != 0) {
    return {};
  }
  return fd;
}

}  // namespace

std::string DBusConnection::SessionBusAddress() {
  const char* address = getenv("DBUS_SESSION_BUS_ADDRESS");
  if (address != nullptr && address[0] != '\0') {
    return address;
  }
  const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
  if (runtime_dir == nullptr || runtime_dir[0] == '\0') {
    return "";
  }
  return std::string("unix:path=") + runtime_dir + "/bus";
}

std::unique_ptr<DBusConnection> DBusConnection::Connect(
    EventLoop* loop,
    std::string_view address) {
  for (std::string_view entry : Split(address, ';')) {
    fml::UniqueFD fd = ConnectUnix(entry);
    if (fd.is_valid()) {
      return std::make_unique<DBusConnection>(loop, std::move(fd));
    }
  }
  return nullptr;
}

DBusConnection::DBusConnection(EventLoop* loop, fml::UniqueFD socket)
    : loop_(loop), socket_(std::move(socket)) {
  int flags = fcntl(socket_.get(), F_GETFL);
  fcntl(socket_.get(), F_SETFL, flags | O_NONBLOCK);

  // Authenticate as the user this process runs as, and pipeline the start of
  // the message stream and the Hello call that the bus requires first.
  std::string uid = std::to_string(getuid());
  std::string auth = std::string(1, '\0') + "AUTH EXTERNAL ";
  static constexpr char kHex[] = "0123456789abcdef";
  for (char c : uid) {
    auth.push_back(kHex[static_cast<uint8_t>(c) >> 4]);
    auth.push_back(kHex[static_cast<uint8_t>(c) & 0xf]);
  }
  auth += "\r\nBEGIN\r\n";
  output_.assign(auth.begin(), auth.end());

  watch_ = loop_->AddWatch(socket_.get(), EPOLLIN,
                           [this](uint32_t events) { OnEvents(events); });

  Call(DBusMessage::MethodCall(kBusName, kBusPath, kBusInterface, "Hello"),
       [this](const DBusMessage* reply) {
         std::string name;
         if (reply == nullptr ||
             reply->type() != DBusMessage::Type::kMethodReturn ||
             !reply->reader().ReadString(&name)) {
           FML_LOG(ERROR) << "Failed to register with D-Bus";
           Disconnect();
           return;
         }
         unique_name_ = name;
         if (ready_callback_) {
           ready_callback_();
         }
       });
}

DBusConnection::~DBusConnection() {
  alive_.reset();
  if (watch_ != 0) {
    loop_->RemoveWatch(watch_);
  }
}

void DBusConnection::Send(const DBusMessage& message) {
  if (!socket_.is_valid()) {
    return;
  }
  Write(message.Serialize(next_serial_++));
}

void DBusConnection::Call(const DBusMessage& message,
                          ReplyCallback callback,
                          std::chrono::milliseconds timeout) {
  uint32_t serial = next_serial_++;
  if (next_serial_ == 0) {
    next_serial_ = 1;
  }
  if (!socket_.is_valid()) {
    // Report the failure asynchronously, as if the reply was lost.
    std::weak_ptr<bool> alive = alive_;
    loop_->PostTask([alive, callback = std::move(callback)]() {
      if (!alive.expired()) {
        callback(nullptr);
      }
    });
    return;
  }
  pending_calls_[serial] = {std::move(callback)};
  Write(message.Serialize(serial));

  std::weak_ptr<bool> alive = alive_;
  loop_->PostTask(
      [this, alive, serial]() {
        if (!alive.expired()) {
          OnTimeout(serial);
        }
      },
      EventLoop::Clock::now() + timeout);
}

void DBusConnection::AddMatch(std::string_view rule) {
  DBusMessage message =
      DBusMessage::MethodCall(kBusName, kBusPath, kBusInterface, "AddMatch");
  message.writer().WriteString(rule);
  message.set_flags(DBusMessage::kNoReplyExpected);
  Send(message);
}

void DBusConnection::Write(std::vector<uint8_t> data) {
  output_.insert(output_.end(), data.begin(), data.end());
  Flush();
}

void DBusConnection::Flush() {
  if (!socket_.is_valid() || write_failed_) {
    return;
  }
  size_t written = 0;
  while (written < output_.size()) {
    ssize_t result = send(socket_.get(), output_.data() + written,
                          output_.size() - written, MSG_NOSIGNAL);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno != EAGAIN) {
        // Report the loss from the loop, not from inside the caller.
        write_failed_ = true;
        std::weak_ptr<bool> alive = alive_;
        loop_->PostTask([this, alive]() {
          if (!alive.expired()) {
            Disconnect();
          }
        });
      }
      break;
    }
    written += result;
  }
  output_.erase(output_.begin(), output_.begin() + written);
  loop_->SetWatchEvents(watch_,
                        EPOLLIN | (output_.empty() ? 0u : uint32_t{EPOLLOUT}));
}

void DBusConnection::OnEvents(uint32_t events) {
  if (events & EPOLLOUT) {
    Flush();
  }
  if (events & (EPOLLIN | EPOLLERR | EPOLLHUP)) {
    ProcessInput();
  }
}

bool DBusConnection::ProcessInput() {
  bool closed = false;
  while (true) {
    uint8_t buffer[4096];
    ssize_t result = recv(socket_.get(), buffer, sizeof(buffer), 0);
    if (result > 0) {
      input_.insert(input_.end(), buffer, buffer + result);
      continue;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    closed = result == 0 || errno != EAGAIN;
    break;
  }

  std::weak_ptr<bool> alive = alive_;
  if (!authenticated_ && !ProcessAuthentication()) {
    return false;
  }

  size_t offset = 0;
  while (authenticated_ && input_.size() - offset >= 16) {
    size_t length =
        DBusMessage::GetLength(input_.data() + offset, input_.size() - offset);
    if (length == 0) {
      FML_LOG(ERROR) << "Invalid D-Bus message header";
      Disconnect();
      return false;
    }
    if (input_.size() - offset < length) {
      break;
    }
    std::optional<DBusMessage> message =
        DBusMessage::Parse(input_.data() + offset, length);
    offset += length;
    if (!message.has_value()) {
      FML_LOG(ERROR) << "Invalid D-Bus message";
      Disconnect();
      return false;
    }
    if (!Dispatch(*message) || alive.expired() || !socket_.is_valid()) {
      return false;
    }
  }
  input_.erase(input_.begin(), input_.begin() + offset);

  if (closed) {
    Disconnect();
    return false;
  }
  return true;
}

bool DBusConnection::ProcessAuthentication() {
  static constexpr std::string_view kNewline = "\r\n";
  auto end = std::search(input_.begin(), input_.end(), kNewline.begin(),
                         kNewline.end());
  if (end == input_.end()) {
    return true;
  }
  std::string line(input_.begin(), end);
  input_.erase(input_.begin(), end + 2);
  if (line.compare(0, 3, "OK ") != 0) {
    FML_LOG(ERROR) << "D-Bus authentication failed: " << line;
    Disconnect();
    return false;
  }
  authenticated_ = true;
  return true;
}

bool DBusConnection::Dispatch(const DBusMessage& message) {
  std::weak_ptr<bool> alive = alive_;
  switch (message.type()) {
    case DBusMessage::Type::kMethodReturn:
    case DBusMessage::Type::kError: {
      auto it = pending_calls_.find(message.reply_serial());
      if (it == pending_calls_.end()) {
        return true;
      }
      ReplyCallback callback = std::move(it->second.callback);
      pending_calls_.erase(it);
      callback(&message);
      return !alive.expired();
    }
    case DBusMessage::Type::kSignal:
      if (signal_handler_) {
        signal_handler_(message);
      }
      return !alive.expired();
    case DBusMessage::Type::kMethodCall: {
      if (message.interface() == kPeerInterface && message.member() == "Ping") {
        if (message.expects_reply()) {
          Send(DBusMessage::MethodReturn(message));
        }
        return true;
      }
      bool handled = method_handler_ && method_handler_(message);
      if (alive.expired()) {
        return false;
      }
      if (!handled && message.expects_reply()) {
        Send(DBusMessage::Error(
            message, "org.freedesktop.DBus.Error.UnknownMethod",
            "Unknown method " + message.interface() + "." + message.member() +
                " on " + message.path()));
      }
      return true;
    }
    default:
      return true;
  }
}

void DBusConnection::OnTimeout(uint32_t serial) {
  auto it = pending_calls_.find(serial);
  if (it == pending_calls_.end()) {
    return;
  }
  ReplyCallback callback = std::move(it->second.callback);
  pending_calls_.erase(it);
  callback(nullptr);
}

void DBusConnection::Disconnect() {
  if (!socket_.is_valid()) {
    return;
  }
  loop_->RemoveWatch(watch_);
  watch_ = 0;
  socket_.reset();
  unique_name_.clear();
  output_.clear();
  input_.clear();

  std::weak_ptr<bool> alive = alive_;
  std::map<uint32_t, PendingCall> pending_calls;
  pending_calls.swap(pending_calls_);
  for (auto& [serial, call] : pending_calls) {
    call.callback(nullptr);
    if (alive.expired()) {
      return;
    }
  }
  if (disconnected_callback_) {
    disconnected_callback_();
  }
}

}  // namespace flutter
