// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_CLIPBOARD_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_CLIPBOARD_H_

#include <functional>
#include <optional>
#include <string>

namespace flutter {

// Text shared between applications with copy and paste.
class Clipboard {
 public:
  // Called with the clipboard text, or nullopt if it has none.
  using TextCallback = std::function<void(std::optional<std::string> text)>;

  virtual ~Clipboard() = default;

  // Replaces the clipboard contents with |text|. Returns false if the
  // application isn't allowed to set the clipboard, as it hasn't had recent
  // input.
  virtual bool SetText(const std::string& text) = 0;

  // Reads the clipboard text. |callback| may be called before this returns,
  // and is always called on the platform thread.
  virtual void GetText(TextCallback callback) = 0;

  // True if the clipboard contains text.
  virtual bool HasText() const = 0;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_CLIPBOARD_H_
