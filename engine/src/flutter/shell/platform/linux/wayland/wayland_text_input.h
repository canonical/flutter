// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_TEXT_INPUT_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_TEXT_INPUT_H_

#include <map>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/input_method.h"
#include "flutter/shell/platform/linux/wayland/protocols/text-input-unstable-v3-client-protocol.h"

namespace flutter {

// The input method for a seat, using the text-input-v3 protocol.
//
// The input method follows keyboard focus, and is given to the handler of
// the focused surface.
class WaylandTextInput : public InputMethod {
 public:
  WaylandTextInput(zwp_text_input_manager_v3* manager, wl_seat* seat);
  ~WaylandTextInput() override;

  // Sends input method requests for |surface| to |handler|, or stops sending
  // them if |handler| is nullptr.
  void SetHandler(wl_surface* surface, InputMethodHandler* handler);

  // |InputMethod|
  void Enable() override;
  void Disable() override;
  void SetSurroundingText(const std::string& text,
                          int32_t cursor,
                          int32_t anchor,
                          bool from_input_method) override;
  void SetContentType(const ContentType& content_type) override;
  void SetCursorRectangle(int32_t x,
                          int32_t y,
                          int32_t width,
                          int32_t height) override;
  void Commit() override;

 private:
  static void Enter(void* data,
                    zwp_text_input_v3* text_input,
                    wl_surface* surface);
  static void Leave(void* data,
                    zwp_text_input_v3* text_input,
                    wl_surface* surface);
  static void PreeditString(void* data,
                            zwp_text_input_v3* text_input,
                            const char* text,
                            int32_t cursor_begin,
                            int32_t cursor_end);
  static void CommitString(void* data,
                           zwp_text_input_v3* text_input,
                           const char* text);
  static void DeleteSurroundingText(void* data,
                                    zwp_text_input_v3* text_input,
                                    uint32_t before_length,
                                    uint32_t after_length);
  static void Done(void* data, zwp_text_input_v3* text_input, uint32_t serial);

  static const zwp_text_input_v3_listener kListener;

  // Returns the handler for the focused surface, or nullptr.
  InputMethodHandler* FocusHandler() const;

  zwp_text_input_v3* text_input_;
  std::map<wl_surface*, InputMethodHandler*> handlers_;

  // The surface with text input focus.
  wl_surface* focus_ = nullptr;

  // Changes received since the last done event.
  InputMethodChange pending_;

  // The number of commit requests, which done events are matched against.
  uint32_t commit_count_ = 0;

  FML_DISALLOW_COPY_AND_ASSIGN(WaylandTextInput);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_WAYLAND_TEXT_INPUT_H_
