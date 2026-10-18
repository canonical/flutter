// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_DESKTOP_MESSENGER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_DESKTOP_MESSENGER_H_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/common/public/flutter_messenger.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/platform_messenger.h"

// The messenger behind FlutterDesktopMessengerRef, which plugins use to send
// and receive platform channel messages through the C ABI.
//
// It is reference counted so plugins can keep it on other threads after the
// engine is destroyed. Messages and responses can be sent from any thread,
// and are forwarded to the platform thread. Everything else is called on the
// platform thread.
struct FlutterDesktopMessenger {
 public:
  // Creates a messenger with a reference count of one that forwards to
  // |messenger|, which runs on |event_loop|.
  FlutterDesktopMessenger(flutter::PlatformMessenger* messenger,
                          flutter::EventLoop* event_loop);

  FlutterDesktopMessengerRef ToRef() { return this; }

  FlutterDesktopMessenger* AddRef();
  void Release();

  // Held by plugins while checking IsAvailable and sending from other
  // threads, so the messenger isn't detached during the send.
  std::mutex& mutex() { return mutex_; }

  // True until the messenger is detached.
  bool IsAvailable() const { return messenger_ != nullptr; }

  // Stops forwarding to the platform messenger, which is being destroyed.
  // Messages still waiting for a response are not handled.
  void Detach();

  bool Send(const char* channel,
            const uint8_t* message,
            size_t message_size,
            FlutterDesktopBinaryReply reply,
            void* user_data);

  void SendResponse(const FlutterDesktopMessageResponseHandle* handle,
                    const uint8_t* data,
                    size_t data_length);

  void SetCallback(const char* channel,
                   FlutterDesktopMessageCallback callback,
                   void* user_data);

 private:
  // A message from the framework waiting for a response from a plugin. This
  // is what plugins see as a FlutterDesktopMessageResponseHandle.
  struct PendingResponse {
    flutter::PlatformMessenger::ResponseCallback respond;
  };

  ~FlutterDesktopMessenger();

  // Runs |task| on the platform thread, now if already on it.
  void RunOnPlatformThread(std::function<void()> task);

  std::atomic<flutter::PlatformMessenger*> messenger_;
  flutter::EventLoop* event_loop_;

  // Channels with handlers set by plugins, removed when detached.
  std::set<std::string> channels_;
  std::atomic<int32_t> ref_count_ = 1;
  std::mutex mutex_;

  // Messages waiting for a response. Guarded by |pending_mutex_| as plugins
  // send responses while holding |mutex_|.
  std::mutex pending_mutex_;
  std::set<PendingResponse*> pending_responses_;

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterDesktopMessenger);
};

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_DESKTOP_MESSENGER_H_
