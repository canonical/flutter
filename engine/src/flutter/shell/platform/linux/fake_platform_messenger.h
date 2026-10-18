// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FAKE_PLATFORM_MESSENGER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FAKE_PLATFORM_MESSENGER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "flutter/shell/platform/common/json_message_codec.h"
#include "flutter/shell/platform/common/json_method_codec.h"
#include "flutter/shell/platform/linux/platform_messenger.h"

namespace flutter {
namespace testing {

// A messenger that records the messages sent to the framework.
class FakePlatformMessenger : public PlatformMessenger {
 public:
  struct SentMessage {
    std::string channel;
    std::vector<uint8_t> message;
    ReplyCallback reply;
  };

  // |PlatformMessenger|
  void SendMessage(const std::string& channel,
                   const std::vector<uint8_t>& message,
                   ReplyCallback reply) override {
    sent.push_back({channel, message, std::move(reply)});
  }

  // |PlatformMessenger|
  void SetMessageHandler(const std::string& channel,
                         MessageHandler handler) override {
    if (handler) {
      handlers[channel] = std::move(handler);
    } else {
      handlers.erase(channel);
    }
  }

  // Sends |message| from the framework on |channel|, returning the
  // response.
  std::vector<uint8_t> Receive(const std::string& channel,
                               const std::vector<uint8_t>& message) {
    std::vector<uint8_t> response;
    auto it = handlers.find(channel);
    if (it != handlers.end()) {
      it->second(message.data(), message.size(),
                 [&response](const std::vector<uint8_t>& r) { response = r; });
    }
    return response;
  }

  // Sends a JSON method call from the framework on |channel|.
  std::vector<uint8_t> ReceiveJsonMethodCall(const std::string& channel,
                                             const std::string& method,
                                             const std::string& args_json) {
    auto args = std::make_unique<rapidjson::Document>();
    args->Parse(args_json.c_str());
    MethodCall<rapidjson::Document> call(method, std::move(args));
    return Receive(channel,
                   *JsonMethodCodec::GetInstance().EncodeMethodCall(call));
  }

  // Decodes the JSON message sent at |index|.
  std::unique_ptr<rapidjson::Document> DecodeJson(size_t index) const {
    return JsonMessageCodec::GetInstance().DecodeMessage(sent[index].message);
  }

  std::vector<SentMessage> sent;
  std::map<std::string, MessageHandler> handlers;
};

}  // namespace testing
}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FAKE_PLATFORM_MESSENGER_H_
