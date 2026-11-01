// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/kms/kms_session.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <xf86drm.h>

#include <cstdlib>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/dbus/dbus_connection.h"

namespace flutter {

namespace {

constexpr char kLogindName[] = "org.freedesktop.login1";
constexpr char kManagerPath[] = "/org/freedesktop/login1";
constexpr char kManagerInterface[] = "org.freedesktop.login1.Manager";
constexpr char kSessionInterface[] = "org.freedesktop.login1.Session";
constexpr char kSeatInterface[] = "org.freedesktop.login1.Seat";
constexpr char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";

// The device major number of DRM devices.
constexpr uint32_t kDrmMajor = 226;

// How long to wait for logind to reply.
constexpr auto kLogindTimeout = std::chrono::seconds(5);

bool IsMethodReturn(const std::optional<DBusMessage>& reply) {
  return reply.has_value() && reply->type() == DBusMessage::Type::kMethodReturn;
}

}  // namespace

std::unique_ptr<KmsSession> KmsSession::Create(EventLoop* event_loop,
                                               const std::string& bus_address) {
  std::unique_ptr<KmsSession> session(new KmsSession(event_loop));
  if (!session->InitializeLogind(bus_address.empty()
                                     ? DBusConnection::SystemBusAddress()
                                     : bus_address)) {
    session->connection_.reset();
    session->active_ = true;
    const char* seat = getenv("XDG_SEAT");
    if (seat != nullptr && seat[0] != '\0') {
      session->seat_ = seat;
    }
    FML_LOG(INFO) << "Not in a logind session, opening devices directly";
  }
  return session;
}

KmsSession::KmsSession(EventLoop* event_loop) : event_loop_(event_loop) {}

KmsSession::~KmsSession() {
  if (connection_ != nullptr) {
    DBusMessage message = DBusMessage::MethodCall(
        kLogindName, session_path_, kSessionInterface, "ReleaseControl");
    message.set_flags(DBusMessage::kNoReplyExpected);
    connection_->Send(message);
  }
}

bool KmsSession::InitializeLogind(const std::string& bus_address) {
  connection_ =
      DBusConnection::Connect(event_loop_, bus_address, /*unix_fds=*/true);
  if (connection_ == nullptr) {
    return false;
  }

  // Find the session this process is in.
  std::optional<DBusMessage> reply;
  const char* session_id = getenv("XDG_SESSION_ID");
  if (session_id != nullptr && session_id[0] != '\0') {
    DBusMessage message = DBusMessage::MethodCall(
        kLogindName, kManagerPath, kManagerInterface, "GetSession");
    message.writer().WriteString(session_id);
    reply = Call(std::move(message));
  }
  if (!IsMethodReturn(reply)) {
    DBusMessage message = DBusMessage::MethodCall(
        kLogindName, kManagerPath, kManagerInterface, "GetSessionByPID");
    message.writer().WriteUInt32(getpid());
    reply = Call(std::move(message));
  }
  if (!IsMethodReturn(reply) ||
      !reply->reader().ReadObjectPath(&session_path_)) {
    return false;
  }
  if (!connection_->unix_fds_enabled()) {
    FML_LOG(ERROR) << "The system bus doesn't support passing file descriptors";
    return false;
  }

  std::optional<DBusMessage> seat =
      GetProperty(session_path_, kSessionInterface, "Seat");
  if (!seat.has_value()) {
    return false;
  }
  DBusReader seat_reader = seat->reader();
  std::string seat_id;
  if (!seat_reader.OpenVariant() || !seat_reader.OpenStruct() ||
      !seat_reader.ReadString(&seat_id) ||
      !seat_reader.ReadObjectPath(&seat_path_) || seat_id.empty()) {
    FML_LOG(ERROR) << "The logind session isn't on a seat";
    return false;
  }
  seat_ = seat_id;

  std::optional<DBusMessage> active =
      GetProperty(session_path_, kSessionInterface, "Active");
  if (!active.has_value()) {
    return false;
  }
  DBusReader active_reader = active->reader();
  if (!active_reader.OpenVariant() || !active_reader.ReadBoolean(&active_)) {
    return false;
  }
  std::optional<DBusMessage> lid =
      GetProperty(kManagerPath, kManagerInterface, "LidClosed");
  if (lid.has_value()) {
    DBusReader lid_reader = lid->reader();
    if (!lid_reader.OpenVariant() || !lid_reader.ReadBoolean(&lid_closed_)) {
      lid_closed_ = false;
    }
  }

  FML_LOG(INFO) << "Using logind session " << session_path_ << " on " << seat_
                << (active_ ? "" : ", waiting for it to be in the foreground");

  // Receive the signals logind sends when devices are paused and resumed.
  connection_->set_signal_handler(
      [this](const DBusMessage& signal) { HandleSignal(signal); });
  connection_->set_disconnected_callback(
      []() { FML_LOG(ERROR) << "Lost the connection to logind"; });
  connection_->AddMatch("type='signal',sender='" + std::string(kLogindName) +
                        "',interface='" + kSessionInterface + "',path='" +
                        session_path_ + "'");

  DBusMessage take_control = DBusMessage::MethodCall(
      kLogindName, session_path_, kSessionInterface, "TakeControl");
  take_control.writer().WriteBoolean(false);
  reply = Call(std::move(take_control));
  if (!IsMethodReturn(reply)) {
    FML_LOG(ERROR) << "Failed to take control of the logind session"
                   << (reply.has_value() ? ": " + reply->error_name() : "");
    return false;
  }
  return true;
}

std::optional<DBusMessage> KmsSession::Call(DBusMessage message) {
  std::optional<DBusMessage> reply =
      connection_->CallSync(message, kLogindTimeout);
  if (reply.has_value() && reply->type() == DBusMessage::Type::kError) {
    std::string text;
    reply->reader().ReadString(&text);
    FML_DLOG(INFO) << "logind " << message.member()
                   << " failed: " << reply->error_name() << " " << text;
  }
  return reply;
}

std::optional<DBusMessage> KmsSession::GetProperty(const std::string& path,
                                                   const char* interface,
                                                   const char* name) {
  DBusMessage message =
      DBusMessage::MethodCall(kLogindName, path, kPropertiesInterface, "Get");
  message.writer().WriteString(interface);
  message.writer().WriteString(name);
  std::optional<DBusMessage> reply = Call(std::move(message));
  if (!IsMethodReturn(reply)) {
    return std::nullopt;
  }
  return reply;
}

fml::UniqueFD KmsSession::OpenDevice(const std::string& path, int flags) {
  if (connection_ == nullptr) {
    fml::UniqueFD fd(open(path.c_str(), flags | O_CLOEXEC));
    if (fd.is_valid() && path.rfind("/dev/dri/", 0) == 0 &&
        drmIsMaster(fd.get()) == 0) {
      // Fails if another process is the DRM master, or the process isn't
      // allowed to become it.
      drmSetMaster(fd.get());
    }
    return fd;
  }

  struct stat info;
  if (stat(path.c_str(), &info) != 0 || !S_ISCHR(info.st_mode)) {
    return {};
  }
  DBusMessage message = DBusMessage::MethodCall(
      kLogindName, session_path_, kSessionInterface, "TakeDevice");
  message.writer().WriteUInt32(major(info.st_rdev));
  message.writer().WriteUInt32(minor(info.st_rdev));
  std::optional<DBusMessage> reply = Call(std::move(message));
  uint32_t index = 0;
  if (!IsMethodReturn(reply) || !reply->reader().ReadUnixFd(&index)) {
    return {};
  }
  fml::UniqueFD fd = reply->DupUnixFd(index);
  if (!fd.is_valid()) {
    return {};
  }

  // logind opens devices with O_RDWR | O_CLOEXEC | O_NOCTTY | O_NONBLOCK.
  if ((flags & O_NONBLOCK) == 0) {
    fcntl(fd.get(), F_SETFL, fcntl(fd.get(), F_GETFL) & ~O_NONBLOCK);
  }
  return fd;
}

void KmsSession::CloseDevice(int fd) {
  struct stat info;
  if (connection_ != nullptr && fstat(fd, &info) == 0) {
    DBusMessage message = DBusMessage::MethodCall(
        kLogindName, session_path_, kSessionInterface, "ReleaseDevice");
    message.writer().WriteUInt32(major(info.st_rdev));
    message.writer().WriteUInt32(minor(info.st_rdev));
    message.set_flags(DBusMessage::kNoReplyExpected);
    connection_->Send(message);
  }
  close(fd);
}

void KmsSession::SwitchVt(uint32_t vt) {
  if (connection_ == nullptr) {
    FML_LOG(WARNING) << "Switching virtual terminals requires logind";
    return;
  }
  DBusMessage message = DBusMessage::MethodCall(kLogindName, seat_path_,
                                                kSeatInterface, "SwitchTo");
  message.writer().WriteUInt32(vt);
  message.set_flags(DBusMessage::kNoReplyExpected);
  connection_->Send(message);
}

void KmsSession::HandleSignal(const DBusMessage& signal) {
  if (signal.path() != session_path_ ||
      signal.interface() != kSessionInterface) {
    return;
  }
  DBusReader reader = signal.reader();
  uint32_t device_major = 0;
  uint32_t device_minor = 0;
  if (!reader.ReadUInt32(&device_major) || !reader.ReadUInt32(&device_minor)) {
    return;
  }

  if (signal.member() == "PauseDevice") {
    std::string type;
    reader.ReadString(&type);
    // The display device is paused when the session moves to the
    // background. Input devices are revoked, and reopened by the input
    // library when the session is resumed.
    if (device_major == kDrmMajor) {
      SetActive(false);
    }
    // logind waits for paused devices to be released, unless forced.
    if (type == "pause") {
      DBusMessage message = DBusMessage::MethodCall(
          kLogindName, session_path_, kSessionInterface, "PauseDeviceComplete");
      message.writer().WriteUInt32(device_major);
      message.writer().WriteUInt32(device_minor);
      message.set_flags(DBusMessage::kNoReplyExpected);
      connection_->Send(message);
    }
  } else if (signal.member() == "ResumeDevice") {
    if (device_major == kDrmMajor) {
      SetActive(true);
    }
  }
}

void KmsSession::SetActive(bool active) {
  if (active_ == active) {
    return;
  }
  active_ = active;
  if (active_callback_) {
    active_callback_(active);
  }
}

}  // namespace flutter
