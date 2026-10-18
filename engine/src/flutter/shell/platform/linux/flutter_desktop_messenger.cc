// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/flutter_desktop_messenger.h"

#include <string>
#include <utility>
#include <vector>

#include "flutter/fml/logging.h"

FlutterDesktopMessenger::FlutterDesktopMessenger(
    flutter::PlatformMessenger* messenger,
    flutter::EventLoop* event_loop)
    : messenger_(messenger), event_loop_(event_loop) {}

FlutterDesktopMessenger::~FlutterDesktopMessenger() {
  FML_DCHECK(messenger_ == nullptr);
}

FlutterDesktopMessenger* FlutterDesktopMessenger::AddRef() {
  ref_count_.fetch_add(1);
  return this;
}

void FlutterDesktopMessenger::Release() {
  if (ref_count_.fetch_sub(1) == 1) {
    delete this;
  }
}

void FlutterDesktopMessenger::Detach() {
  FML_DCHECK(event_loop_->RunsTasksOnCurrentThread());
  flutter::PlatformMessenger* messenger = messenger_;
  if (messenger == nullptr) {
    return;
  }
  for (const std::string& channel : channels_) {
    messenger->SetMessageHandler(channel, nullptr);
  }
  channels_.clear();
  {
    std::scoped_lock lock(mutex_);
    messenger_ = nullptr;
  }

  // Destroying the response callbacks tells the framework the messages
  // weren't handled.
  std::set<PendingResponse*> pending;
  {
    std::scoped_lock lock(pending_mutex_);
    pending.swap(pending_responses_);
  }
  for (PendingResponse* response : pending) {
    delete response;
  }
}

void FlutterDesktopMessenger::RunOnPlatformThread(std::function<void()> task) {
  if (event_loop_->RunsTasksOnCurrentThread()) {
    task();
    return;
  }
  // Keep the messenger alive until the task has run.
  event_loop_->PostTask([self = AddRef(), task = std::move(task)]() {
    task();
    self->Release();
  });
}

bool FlutterDesktopMessenger::Send(const char* channel,
                                   const uint8_t* message,
                                   size_t message_size,
                                   FlutterDesktopBinaryReply reply,
                                   void* user_data) {
  if (!IsAvailable()) {
    return false;
  }

  RunOnPlatformThread([this, channel = std::string(channel),
                       message = std::vector<uint8_t>(message,
                                                      message + message_size),
                       reply, user_data]() {
    if (!IsAvailable()) {
      // Let the reply free its user data.
      if (reply != nullptr) {
        reply(nullptr, 0, user_data);
      }
      return;
    }
    flutter::PlatformMessenger::ReplyCallback reply_callback;
    if (reply != nullptr) {
      reply_callback = [reply, user_data](const uint8_t* data, size_t size) {
        reply(data, size, user_data);
      };
    }
    messenger_.load()->SendMessage(channel, message, std::move(reply_callback));
  });
  return true;
}

void FlutterDesktopMessenger::SendResponse(
    const FlutterDesktopMessageResponseHandle* handle,
    const uint8_t* data,
    size_t data_length) {
  // The handle is the pending response, which is only valid until it is
  // used or the messenger is detached.
  auto* response = reinterpret_cast<PendingResponse*>(
      const_cast<FlutterDesktopMessageResponseHandle*>(handle));
  {
    std::scoped_lock lock(pending_mutex_);
    if (pending_responses_.erase(response) == 0) {
      FML_LOG(ERROR) << "Ignoring response to unknown message";
      return;
    }
  }

  RunOnPlatformThread(
      [response, data = std::vector<uint8_t>(data, data + data_length)]() {
        response->respond(data);
        delete response;
      });
}

void FlutterDesktopMessenger::SetCallback(
    const char* channel,
    FlutterDesktopMessageCallback callback,
    void* user_data) {
  FML_DCHECK(event_loop_->RunsTasksOnCurrentThread());
  if (!IsAvailable()) {
    return;
  }
  flutter::PlatformMessenger* messenger = messenger_;
  if (callback == nullptr) {
    messenger->SetMessageHandler(channel, nullptr);
    channels_.erase(channel);
    return;
  }

  channels_.insert(channel);
  messenger->SetMessageHandler(
      channel, [this, channel = std::string(channel), callback, user_data](
                   const uint8_t* data, size_t size,
                   flutter::PlatformMessenger::ResponseCallback respond) {
        auto* response = new PendingResponse{std::move(respond)};
        {
          std::scoped_lock lock(pending_mutex_);
          pending_responses_.insert(response);
        }
        FlutterDesktopMessage message = {};
        message.struct_size = sizeof(FlutterDesktopMessage);
        message.channel = channel.c_str();
        message.message = data;
        message.message_size = size;
        message.response_handle =
            reinterpret_cast<const FlutterDesktopMessageResponseHandle*>(
                response);
        callback(ToRef(), &message, user_data);
      });
}

// The C ABI used by the client wrapper. Defined here so the messenger can be
// used without an engine in tests.

bool FlutterDesktopMessengerSend(FlutterDesktopMessengerRef messenger,
                                 const char* channel,
                                 const uint8_t* message,
                                 const size_t message_size) {
  return messenger->Send(channel, message, message_size, nullptr, nullptr);
}

bool FlutterDesktopMessengerSendWithReply(FlutterDesktopMessengerRef messenger,
                                          const char* channel,
                                          const uint8_t* message,
                                          const size_t message_size,
                                          const FlutterDesktopBinaryReply reply,
                                          void* user_data) {
  return messenger->Send(channel, message, message_size, reply, user_data);
}

void FlutterDesktopMessengerSendResponse(
    FlutterDesktopMessengerRef messenger,
    const FlutterDesktopMessageResponseHandle* handle,
    const uint8_t* data,
    size_t data_length) {
  messenger->SendResponse(handle, data, data_length);
}

void FlutterDesktopMessengerSetCallback(FlutterDesktopMessengerRef messenger,
                                        const char* channel,
                                        FlutterDesktopMessageCallback callback,
                                        void* user_data) {
  messenger->SetCallback(channel, callback, user_data);
}

FlutterDesktopMessengerRef FlutterDesktopMessengerAddRef(
    FlutterDesktopMessengerRef messenger) {
  return messenger->AddRef();
}

void FlutterDesktopMessengerRelease(FlutterDesktopMessengerRef messenger) {
  messenger->Release();
}

bool FlutterDesktopMessengerIsAvailable(FlutterDesktopMessengerRef messenger) {
  return messenger->IsAvailable();
}

FlutterDesktopMessengerRef FlutterDesktopMessengerLock(
    FlutterDesktopMessengerRef messenger) {
  messenger->mutex().lock();
  return messenger;
}

void FlutterDesktopMessengerUnlock(FlutterDesktopMessengerRef messenger) {
  messenger->mutex().unlock();
}
