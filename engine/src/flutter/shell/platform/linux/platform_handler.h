// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_HANDLER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_HANDLER_H_

#include <functional>
#include <memory>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/clipboard.h"
#include "flutter/shell/platform/linux/platform_messenger.h"

namespace flutter {

// Implements the flutter/platform channel: the clipboard, system sounds and
// exiting the application.
class PlatformHandler {
 public:
  // |messenger| and |clipboard| must outlive the handler. |clipboard| may be
  // nullptr if there is none. |on_exit| is called to end the application,
  // and |on_alert| to play the alert sound.
  PlatformHandler(PlatformMessenger* messenger,
                  Clipboard* clipboard,
                  std::function<void()> on_exit,
                  std::function<void()> on_alert);
  ~PlatformHandler();

  // Called when the user asks to close the application. Once the framework
  // has started it may cancel the request, otherwise the application exits.
  void RequestAppExit();

 private:
  // Handles a method call, calling |respond| with the encoded response.
  void HandleMethodCall(const uint8_t* data,
                        size_t size,
                        const PlatformMessenger::ResponseCallback& respond);

  // Asks the framework if the application may exit, calling |callback| with
  // true if it may.
  void SendRequestAppExit(const char* type,
                          std::function<void(bool exit)> callback);

  PlatformMessenger* messenger_;
  Clipboard* clipboard_;
  std::function<void()> on_exit_;
  std::function<void()> on_alert_;

  // True once the framework is ready to handle exit requests.
  bool initialization_complete_ = false;

  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(PlatformHandler);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_HANDLER_H_
