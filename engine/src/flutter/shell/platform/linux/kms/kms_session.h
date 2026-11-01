// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_SESSION_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_SESSION_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "flutter/fml/macros.h"
#include "flutter/fml/unique_fd.h"
#include "flutter/shell/platform/linux/dbus/dbus_message.h"
#include "flutter/shell/platform/linux/event_loop.h"

namespace flutter {

class DBusConnection;

// Access to the display and input devices of the seat the application runs
// on.
//
// If the application runs in a logind session, control of the session is
// taken and devices are opened through logind, which revokes them while the
// user is switched to another virtual terminal. Otherwise devices are opened
// directly, which requires permission to access them (e.g. running as root).
class KmsSession {
 public:
  // Connects to logind if possible. |bus_address| is the system bus address,
  // or empty to use the default.
  static std::unique_ptr<KmsSession> Create(
      EventLoop* event_loop,
      const std::string& bus_address = "");

  ~KmsSession();

  // The seat the session is on, e.g. "seat0".
  const std::string& seat() const { return seat_; }

  // True if the session is in the foreground, so devices can be used.
  bool active() const { return active_; }

  // True if devices are opened through logind.
  bool uses_logind() const { return connection_ != nullptr; }

  // Called when the session moves to the foreground or background.
  void set_active_callback(std::function<void(bool active)> callback) {
    active_callback_ = std::move(callback);
  }

  // Opens the device at |path| with |flags| (O_RDWR, O_NONBLOCK...). DRM
  // devices opened through logind are made the DRM master. Returns an
  // invalid file descriptor on failure.
  fml::UniqueFD OpenDevice(const std::string& path, int flags);

  // Releases a device opened with OpenDevice and closes |fd|.
  void CloseDevice(int fd);

  // Switches to virtual terminal |vt|. Only supported with logind.
  void SwitchVt(uint32_t vt);

 private:
  explicit KmsSession(EventLoop* event_loop);

  // Takes control of the logind session. Returns false if there is no
  // session that can be controlled.
  bool InitializeLogind(const std::string& bus_address);

  // Sends the method call |message| and returns the reply, or nullopt if
  // there was no reply.
  std::optional<DBusMessage> Call(DBusMessage message);

  // Reads a property of the session. Returns nullopt on failure.
  std::optional<DBusMessage> GetSessionProperty(const char* name);

  void HandleSignal(const DBusMessage& signal);

  void SetActive(bool active);

  EventLoop* event_loop_;
  std::unique_ptr<DBusConnection> connection_;
  std::string session_path_;
  std::string seat_path_;
  std::string seat_ = "seat0";
  bool active_ = true;
  std::function<void(bool)> active_callback_;

  FML_DISALLOW_COPY_AND_ASSIGN(KmsSession);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_SESSION_H_
