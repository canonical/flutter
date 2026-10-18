// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_MESSENGER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_MESSENGER_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace flutter {

// Sends and receives messages on platform channels. Used on the platform
// thread.
class PlatformMessenger {
 public:
  // Called with the framework's response to a message. |data| is nullptr and
  // |size| is zero if nothing in the framework handled the message.
  using ReplyCallback = std::function<void(const uint8_t* data, size_t size)>;

  // Sends the response to a message. An empty response means the message was
  // not handled.
  using ResponseCallback =
      std::function<void(const std::vector<uint8_t>& response)>;

  // Handles a message from the framework. |respond| must be called once. If
  // it is destroyed without being called an empty response is sent.
  using MessageHandler = std::function<
      void(const uint8_t* data, size_t size, ResponseCallback respond)>;

  virtual ~PlatformMessenger() = default;

  // Sends |message| to the framework on |channel|. |reply| may be empty if
  // the response is not needed.
  virtual void SendMessage(const std::string& channel,
                           const std::vector<uint8_t>& message,
                           ReplyCallback reply) = 0;

  // Sets the handler for messages on |channel|, or removes it if |handler|
  // is empty.
  virtual void SetMessageHandler(const std::string& channel,
                                 MessageHandler handler) = 0;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_MESSENGER_H_
