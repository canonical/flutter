// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_TEXT_INPUT_PLUGIN_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_TEXT_INPUT_PLUGIN_H_

#include <memory>
#include <string>

#include <rapidjson/document.h>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/common/text_input_model.h"
#include "flutter/shell/platform/linux/key_event_converter.h"
#include "flutter/shell/platform/linux/platform_messenger.h"

namespace flutter {

struct TextEditingDelta;

// Implements the flutter/textinput channel, editing text fields with the keys
// Flutter didn't handle.
//
// Keys that edit text without typing, such as Backspace and the arrow keys,
// are handled by the framework's shortcuts.
class TextInputPlugin {
 public:
  // |messenger| must outlive the plugin.
  explicit TextInputPlugin(PlatformMessenger* messenger);
  ~TextInputPlugin();

  // Handles a key press or repeat that Flutter didn't handle. Returns true if
  // the key was used.
  bool HandleKey(const KeyboardKey& key, const KeyModifiers& modifiers);

 private:
  // Handles a method call from the framework, returning the encoded
  // response.
  std::vector<uint8_t> HandleMethodCall(const uint8_t* data, size_t size);

  // Types |text| into the text field.
  void AddText(const std::string& text);

  // Responds to the Enter key.
  void EnterPressed();

  // Informs the framework of the text field's new state, after |delta| was
  // applied.
  void SendStateUpdate(const TextEditingDelta& delta);

  // Invokes |method| on the framework with |args|.
  void InvokeMethod(const std::string& method,
                    std::unique_ptr<rapidjson::Document> args);

  PlatformMessenger* messenger_;

  // The text field being edited, or nullptr if none.
  std::unique_ptr<TextInputModel> active_model_;

  // The framework's identifier for the text field.
  int client_id_ = 0;

  // The input action and type of the text field, e.g.
  // "TextInputAction.done" and "TextInputType.text".
  std::string input_action_;
  std::string input_type_;

  // True if the framework wants changes as deltas.
  bool enable_delta_model_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(TextInputPlugin);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_TEXT_INPUT_PLUGIN_H_
