// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_PLUGIN_MESSENGER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_PLUGIN_MESSENGER_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/platform_messenger.h"

namespace flutter {

// Lets plugins exchange messages with the framework through an engine's
// PlatformMessenger, which may only be used on the platform thread.
//
// Messages can be sent and responded to from any thread; they are forwarded
// to the platform thread. Handlers and replies are called on the platform
// thread.
//
// Reference counted, as messages being forwarded and responses plugins have
// yet to send may outlive the engine. Once detached from the engine, messages
// are dropped.
class PluginMessenger {
 public:
  // Called with the message data and a response ID to pass to SendResponse.
  using MessageHandler = std::function<
      void(const uint8_t* data, size_t size, uint64_t response_id)>;

  // Called with the reply, or nullptr if there was no reply.
  using ReplyCallback = std::function<void(const uint8_t* data, size_t size)>;

  // Calls a reply callback exactly once, with no reply if it is destroyed
  // without being called.
  class ReplyOnce {
   public:
    explicit ReplyOnce(ReplyCallback reply) : reply_(std::move(reply)) {}
    ~ReplyOnce() { Reply(nullptr, 0); }

    void Reply(const uint8_t* data, size_t size) {
      ReplyCallback reply = std::move(reply_);
      reply_ = nullptr;
      if (reply) {
        reply(data, size);
      }
    }

   private:
    ReplyCallback reply_;

    FML_DISALLOW_COPY_AND_ASSIGN(ReplyOnce);
  };

  // Creates a messenger with a reference count of one that forwards to
  // |messenger|, which runs on |event_loop|. The event loop must outlive the
  // messenger.
  PluginMessenger(PlatformMessenger* messenger, EventLoop* event_loop);

  PluginMessenger* AddRef();
  void Release();

  // Stops forwarding messages. Removes the handlers set with this messenger
  // and responds to unanswered messages with empty responses. Called on the
  // platform thread before the engine is destroyed.
  void Detach();

  // Sends |message| on |channel|. |reply| is always called exactly once on
  // the platform thread (with nullptr if there was no reply or the messenger
  // was detached). May be called from any thread.
  void Send(const std::string& channel,
            std::vector<uint8_t> message,
            ReplyCallback reply);

  // Sets the handler for messages on |channel|, replacing any existing
  // handler. An empty handler removes it. Called on the platform thread.
  void SetMessageHandler(const std::string& channel, MessageHandler handler);

  // Responds to the message the handler was called with |response_id| for.
  // Must be called exactly once per message. May be called from any thread.
  void SendResponse(uint64_t response_id, std::vector<uint8_t> response);

 private:
  ~PluginMessenger();

  // Runs |task| on the platform thread, now if already on it.
  void RunOnPlatformThread(std::function<void()> task);

  bool attached() const { return messenger_ != nullptr; }

  std::atomic<int> ref_count_ = 1;

  // The engine's messenger, or nullptr once detached. Only used on the
  // platform thread.
  PlatformMessenger* messenger_;
  EventLoop* event_loop_;

  // Handlers set by plugins, which are removed when detached.
  std::map<std::string, MessageHandler> handlers_;

  // Responses plugins have yet to send. Only used on the platform thread.
  std::map<uint64_t, PlatformMessenger::ResponseCallback> pending_responses_;
  uint64_t next_response_id_ = 1;

  // Replies to messages sent by plugins the engine has yet to deliver. Only
  // used on the platform thread.
  std::map<uint64_t, std::shared_ptr<ReplyOnce>> pending_replies_;
  uint64_t next_reply_id_ = 1;

  FML_DISALLOW_COPY_AND_ASSIGN(PluginMessenger);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_PLUGIN_MESSENGER_H_
