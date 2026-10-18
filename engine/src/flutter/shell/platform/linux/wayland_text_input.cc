// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland_text_input.h"

namespace flutter {

const zwp_text_input_v3_listener WaylandTextInput::kListener = {
    .enter = Enter,
    .leave = Leave,
    .preedit_string = PreeditString,
    .commit_string = CommitString,
    .delete_surrounding_text = DeleteSurroundingText,
    .done = Done,
};

WaylandTextInput::WaylandTextInput(zwp_text_input_manager_v3* manager,
                                   wl_seat* seat)
    : text_input_(zwp_text_input_manager_v3_get_text_input(manager, seat)) {
  zwp_text_input_v3_add_listener(text_input_, &kListener, this);
}

WaylandTextInput::~WaylandTextInput() {
  zwp_text_input_v3_destroy(text_input_);
}

void WaylandTextInput::SetHandler(wl_surface* surface,
                                  InputMethodHandler* handler) {
  if (handler != nullptr) {
    handlers_[surface] = handler;
    return;
  }
  auto it = handlers_.find(surface);
  if (it == handlers_.end()) {
    return;
  }
  InputMethodHandler* old_handler = it->second;
  handlers_.erase(it);
  if (focus_ == surface) {
    focus_ = nullptr;
    old_handler->OnInputMethodLeave();
  }
}

void WaylandTextInput::Enable() {
  zwp_text_input_v3_enable(text_input_);
}

void WaylandTextInput::Disable() {
  zwp_text_input_v3_disable(text_input_);
}

void WaylandTextInput::SetSurroundingText(const std::string& text,
                                          int32_t cursor,
                                          int32_t anchor,
                                          bool from_input_method) {
  zwp_text_input_v3_set_surrounding_text(text_input_, text.c_str(), cursor,
                                         anchor);
  zwp_text_input_v3_set_text_change_cause(
      text_input_, from_input_method
                       ? ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_INPUT_METHOD
                       : ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER);
}

void WaylandTextInput::SetContentType(const ContentType& content_type) {
  zwp_text_input_v3_set_content_type(text_input_, content_type.hint,
                                     content_type.purpose);
}

void WaylandTextInput::SetCursorRectangle(int32_t x,
                                          int32_t y,
                                          int32_t width,
                                          int32_t height) {
  zwp_text_input_v3_set_cursor_rectangle(text_input_, x, y, width, height);
}

void WaylandTextInput::Commit() {
  zwp_text_input_v3_commit(text_input_);
  commit_count_++;
}

InputMethodHandler* WaylandTextInput::FocusHandler() const {
  auto it = handlers_.find(focus_);
  return it != handlers_.end() ? it->second : nullptr;
}

void WaylandTextInput::Enter(void* data,
                             zwp_text_input_v3* text_input,
                             wl_surface* surface) {
  auto* self = static_cast<WaylandTextInput*>(data);
  self->focus_ = surface;
  self->pending_ = {};
  InputMethodHandler* handler = self->FocusHandler();
  if (handler != nullptr) {
    handler->OnInputMethodEnter(self);
  }
}

void WaylandTextInput::Leave(void* data,
                             zwp_text_input_v3* text_input,
                             wl_surface* surface) {
  auto* self = static_cast<WaylandTextInput*>(data);
  InputMethodHandler* handler = self->FocusHandler();
  self->focus_ = nullptr;
  if (handler != nullptr) {
    handler->OnInputMethodLeave();
  }
}

void WaylandTextInput::PreeditString(void* data,
                                     zwp_text_input_v3* text_input,
                                     const char* text,
                                     int32_t cursor_begin,
                                     int32_t cursor_end) {
  auto* self = static_cast<WaylandTextInput*>(data);
  self->pending_.preedit = text != nullptr ? text : "";
  self->pending_.preedit_cursor_begin = cursor_begin;
  self->pending_.preedit_cursor_end = cursor_end;
}

void WaylandTextInput::CommitString(void* data,
                                    zwp_text_input_v3* text_input,
                                    const char* text) {
  auto* self = static_cast<WaylandTextInput*>(data);
  self->pending_.commit = text != nullptr ? text : "";
}

void WaylandTextInput::DeleteSurroundingText(void* data,
                                             zwp_text_input_v3* text_input,
                                             uint32_t before_length,
                                             uint32_t after_length) {
  auto* self = static_cast<WaylandTextInput*>(data);
  self->pending_.delete_before = before_length;
  self->pending_.delete_after = after_length;
}

void WaylandTextInput::Done(void* data,
                            zwp_text_input_v3* text_input,
                            uint32_t serial) {
  auto* self = static_cast<WaylandTextInput*>(data);
  // Each done event carries the complete state, so unsent values reset to
  // their defaults.
  InputMethodChange change = std::move(self->pending_);
  self->pending_ = {};
  change.current = serial == self->commit_count_;
  InputMethodHandler* handler = self->FocusHandler();
  if (handler != nullptr) {
    handler->OnInputMethodChange(change);
  }
}

}  // namespace flutter
