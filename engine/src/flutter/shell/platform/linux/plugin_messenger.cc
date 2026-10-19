// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/plugin_messenger.h"

#include <utility>

#include "flutter/fml/logging.h"

namespace flutter {

PluginMessenger::PluginMessenger(PlatformMessenger* messenger,
                                 EventLoop* event_loop)
    : messenger_(messenger), event_loop_(event_loop) {}

PluginMessenger::~PluginMessenger() {
  FML_DCHECK(!attached());
}

PluginMessenger* PluginMessenger::AddRef() {
  ref_count_.fetch_add(1);
  return this;
}

void PluginMessenger::Release() {
  if (ref_count_.fetch_sub(1) == 1) {
    delete this;
  }
}

void PluginMessenger::Detach() {
  FML_DCHECK(event_loop_->RunsTasksOnCurrentThread());
  if (!attached()) {
    return;
  }
  for (const auto& [channel, handler] : handlers_) {
    messenger_->SetMessageHandler(channel, nullptr);
  }
  handlers_.clear();

  // Replies the engine has yet to deliver won't arrive once it is destroyed.
  std::map<uint64_t, std::shared_ptr<ReplyOnce>> replies =
      std::move(pending_replies_);
  pending_replies_.clear();
  for (auto& [id, reply] : replies) {
    reply->Reply(nullptr, 0);
  }

  // Destroying the response callbacks tells the framework the messages
  // weren't handled. Plugins may still send responses, which are ignored.
  pending_responses_.clear();

  messenger_ = nullptr;
}

void PluginMessenger::RunOnPlatformThread(std::function<void()> task) {
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

void PluginMessenger::Send(const std::string& channel,
                           std::vector<uint8_t> message,
                           ReplyCallback reply) {
  auto reply_once = std::make_shared<ReplyOnce>(std::move(reply));
  RunOnPlatformThread([this, channel, message = std::move(message),
                       reply_once = std::move(reply_once)]() {
    if (!attached()) {
      return;
    }
    uint64_t reply_id = next_reply_id_++;
    pending_replies_[reply_id] = reply_once;
    messenger_->SendMessage(
        channel, message, [this, reply_id](const uint8_t* data, size_t size) {
          auto it = pending_replies_.find(reply_id);
          if (it == pending_replies_.end()) {
            return;
          }
          std::shared_ptr<ReplyOnce> reply = std::move(it->second);
          pending_replies_.erase(it);
          reply->Reply(data, size);
        });
  });
}

void PluginMessenger::SetMessageHandler(const std::string& channel,
                                        MessageHandler handler) {
  FML_DCHECK(event_loop_->RunsTasksOnCurrentThread());
  if (!attached()) {
    return;
  }
  if (!handler) {
    handlers_.erase(channel);
    messenger_->SetMessageHandler(channel, nullptr);
    return;
  }

  handlers_[channel] = std::move(handler);
  messenger_->SetMessageHandler(
      channel, [this, channel](const uint8_t* data, size_t size,
                               PlatformMessenger::ResponseCallback respond) {
        uint64_t response_id = next_response_id_++;
        pending_responses_[response_id] = std::move(respond);
        // Copy the handler as it may replace itself.
        MessageHandler handler = handlers_[channel];
        handler(data, size, response_id);
      });
}

void PluginMessenger::SendResponse(uint64_t response_id,
                                   std::vector<uint8_t> response) {
  RunOnPlatformThread([this, response_id, response = std::move(response)]() {
    auto it = pending_responses_.find(response_id);
    if (it == pending_responses_.end()) {
      // The messenger was detached.
      return;
    }
    PlatformMessenger::ResponseCallback respond = std::move(it->second);
    pending_responses_.erase(it);
    respond(response);
  });
}

}  // namespace flutter
