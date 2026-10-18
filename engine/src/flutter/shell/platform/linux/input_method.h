// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_INPUT_METHOD_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_INPUT_METHOD_H_

#include <cstdint>
#include <string>

namespace flutter {

// Changes to a text field requested by an input method. Offsets and lengths
// are in UTF-8 bytes.
struct InputMethodChange {
  // The text being composed, shown at the cursor in place of any previous
  // composing text.
  std::string preedit;

  // The selection in |preedit|, or -1 if the cursor should be hidden.
  int32_t preedit_cursor_begin = -1;
  int32_t preedit_cursor_end = -1;

  // Text to insert at the cursor, after removing the previous composing text
  // and deleting the surrounding text.
  std::string commit;

  // The number of bytes to delete before and after the selection.
  uint32_t delete_before = 0;
  uint32_t delete_after = 0;

  // False if the input method had not seen the most recent state sent to it
  // when it made this change.
  bool current = true;
};

// The compositor's input method, used to enter text that can't be typed
// directly, such as with an on-screen keyboard or a CJK input method.
//
// State changes are buffered until Commit is called.
class InputMethod {
 public:
  // Hints and purposes from the text-input-v3 protocol.
  struct ContentType {
    uint32_t hint = 0;
    uint32_t purpose = 0;
  };

  virtual ~InputMethod() = default;

  // Starts or stops using the input method for the focused text field.
  // Enabling resets all state.
  virtual void Enable() = 0;
  virtual void Disable() = 0;

  // Sets the text around the cursor, excluding composing text. |cursor| and
  // |anchor| are byte offsets in |text|. |from_input_method| is true if the
  // text changed because of the input method.
  virtual void SetSurroundingText(const std::string& text,
                                  int32_t cursor,
                                  int32_t anchor,
                                  bool from_input_method) = 0;

  virtual void SetContentType(const ContentType& content_type) = 0;

  // Sets the area of the cursor or composing text, in surface coordinates.
  virtual void SetCursorRectangle(int32_t x,
                                  int32_t y,
                                  int32_t width,
                                  int32_t height) = 0;

  // Applies the state changes since the last commit.
  virtual void Commit() = 0;
};

// Receives requests from an input method for the text fields in a surface.
// Called on the platform thread.
class InputMethodHandler {
 public:
  virtual ~InputMethodHandler() = default;

  // Called when |input_method| can be used for the surface. It remains valid
  // until OnInputMethodLeave.
  virtual void OnInputMethodEnter(InputMethod* input_method) = 0;

  // Called when the input method can no longer be used for the surface.
  virtual void OnInputMethodLeave() = 0;

  // Called when the input method changes the text.
  virtual void OnInputMethodChange(const InputMethodChange& change) = 0;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_INPUT_METHOD_H_
