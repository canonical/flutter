// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_TEXT_INPUT_PLUGIN_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_TEXT_INPUT_PLUGIN_H_

#include <array>
#include <memory>
#include <optional>
#include <string>

#include <rapidjson/document.h>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/common/text_input_model.h"
#include "flutter/shell/platform/linux/input_method.h"
#include "flutter/shell/platform/linux/key_event_converter.h"
#include "flutter/shell/platform/linux/platform_messenger.h"

namespace flutter {

struct TextEditingDelta;

// Implements the flutter/textinput channel, editing text fields with the keys
// Flutter didn't handle and the compositor's input method.
//
// Keys that edit text without typing, such as Backspace and the arrow keys,
// are handled by the framework's shortcuts.
class TextInputPlugin : public InputMethodHandler {
 public:
  // |messenger| must outlive the plugin.
  explicit TextInputPlugin(PlatformMessenger* messenger);
  ~TextInputPlugin() override;

  // Handles a key press or repeat that Flutter didn't handle. Returns true if
  // the key was used.
  bool HandleKey(const KeyboardKey& key, const KeyModifiers& modifiers);

  // |InputMethodHandler|
  void OnInputMethodEnter(InputMethod* input_method) override;
  void OnInputMethodLeave() override;
  void OnInputMethodChange(const InputMethodChange& change) override;

 private:
  // A rectangle in logical pixels.
  struct Rect {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
  };

  // A rectangle in surface coordinates.
  struct IntRect {
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;

    bool operator==(const IntRect& other) const = default;
  };

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

  // Starts using the input method for the text field, resetting its state.
  void EnableInputMethod();

  // Sends the text field's state to the input method and commits it.
  // |from_input_method| is true if the state changed because of the input
  // method.
  void SendInputMethodState(bool from_input_method);

  // Sends the cursor rectangle to the input method if it has changed.
  void UpdateInputMethodCursor();

  // Returns the area of the composing text or cursor, in surface
  // coordinates.
  IntRect GetCursorRectangle() const;

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

  // The kind of text the field contains, for the input method.
  InputMethod::ContentType content_type_;

  // The area of the composing text or cursor in the text field, and the
  // column-major transform from the text field to the view.
  Rect marked_text_rect_;
  std::array<double, 16> editable_transform_ = {1, 0, 0, 0, 0, 1, 0, 0,
                                                0, 0, 1, 0, 0, 0, 0, 1};

  // The input method for the focused surface, or nullptr if there is none.
  InputMethod* input_method_ = nullptr;

  // True if the input method is being used for the text field.
  bool input_method_enabled_ = false;

  // True if the text field changed since the input method was last sent its
  // state.
  bool input_method_state_dirty_ = false;

  // The cursor rectangle last sent to the input method.
  std::optional<IntRect> input_method_cursor_;

  FML_DISALLOW_COPY_AND_ASSIGN(TextInputPlugin);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_TEXT_INPUT_PLUGIN_H_
