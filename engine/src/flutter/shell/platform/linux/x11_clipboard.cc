// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/x11_clipboard.h"

#include <chrono>
#include <cstdlib>
#include <cstring>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/x11_display.h"

namespace flutter {

namespace {

// How long to wait for another application to provide the clipboard
// contents.
constexpr auto kRequestTimeout = std::chrono::seconds(2);

// Whether a ChangeProperty request with |size| bytes of data fits in the
// server's maximum request length. Larger requests close the connection.
bool FitsInRequest(xcb_connection_t* connection, size_t size) {
  // The maximum length is in 4-byte units, and the request header is 24
  // bytes.
  uint64_t max_bytes =
      static_cast<uint64_t>(xcb_get_maximum_request_length(connection)) * 4;
  return size + 24 <= max_bytes;
}

}  // namespace

std::unique_ptr<X11Clipboard> X11Clipboard::Create(X11Display* display) {
  std::unique_ptr<X11Clipboard> clipboard(new X11Clipboard(display));
  if (!clipboard->Initialize()) {
    return nullptr;
  }
  return clipboard;
}

X11Clipboard::X11Clipboard(X11Display* display) : display_(display) {}

X11Clipboard::~X11Clipboard() {
  for (TextCallback& callback : callbacks_) {
    callback(std::nullopt);
  }
  if (window_ != XCB_NONE) {
    xcb_destroy_window(display_->connection(), window_);
  }
}

bool X11Clipboard::Initialize() {
  xcb_connection_t* connection = display_->connection();
  window_ = xcb_generate_id(connection);
  uint32_t event_mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
  xcb_create_window(connection, XCB_COPY_FROM_PARENT, window_,
                    display_->screen()->root, -1, -1, 1, 1, 0,
                    XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT,
                    XCB_CW_EVENT_MASK, &event_mask);
  return true;
}

bool X11Clipboard::SetText(const std::string& text) {
  xcb_set_selection_owner(display_->connection(), window_,
                          display_->atom(X11Display::Atom::kClipboard),
                          display_->last_input_time());
  display_->Flush();
  text_ = text;
  return true;
}

void X11Clipboard::GetText(TextCallback callback) {
  if (text_.has_value()) {
    callback(*text_);
    return;
  }
  callbacks_.push_back(std::move(callback));
  if (callbacks_.size() == 1) {
    RequestText();
  }
}

bool X11Clipboard::HasText() const {
  if (text_.has_value()) {
    return true;
  }
  // Assume the owner provides text, as asking requires waiting for it.
  xcb_connection_t* connection = display_->connection();
  xcb_get_selection_owner_reply_t* reply = xcb_get_selection_owner_reply(
      connection,
      xcb_get_selection_owner(connection,
                              display_->atom(X11Display::Atom::kClipboard)),
      nullptr);
  bool has_owner = reply != nullptr && reply->owner != XCB_NONE;
  free(reply);
  return has_owner;
}

void X11Clipboard::RequestText() {
  if (callbacks_.empty()) {
    return;
  }
  if (text_.has_value()) {
    FinishRequest(*text_);
    return;
  }

  receiving_incr_ = false;
  received_.clear();
  requested_target_ = display_->atom(X11Display::Atom::kUtf8String);
  xcb_convert_selection(display_->connection(), window_,
                        display_->atom(X11Display::Atom::kClipboard),
                        requested_target_,
                        display_->atom(X11Display::Atom::kSelectionProperty),
                        display_->last_input_time());
  display_->Flush();

  uint64_t generation = ++request_generation_;
  display_->event_loop()->PostTask(
      [this, alive = std::weak_ptr<bool>(alive_), generation]() {
        if (!alive.expired() && generation == request_generation_ &&
            !callbacks_.empty()) {
          FML_LOG(WARNING) << "Timed out reading the clipboard";
          FinishRequest(std::nullopt);
        }
      },
      EventLoop::Clock::now() + kRequestTimeout);
}

void X11Clipboard::FinishRequest(std::optional<std::string> text) {
  request_generation_++;
  receiving_incr_ = false;
  received_.clear();
  if (callbacks_.empty()) {
    return;
  }
  TextCallback callback = std::move(callbacks_.front());
  callbacks_.pop_front();
  callback(std::move(text));
  RequestText();
}

bool X11Clipboard::HandleEvent(xcb_generic_event_t* event) {
  switch (event->response_type & ~0x80) {
    case XCB_SELECTION_REQUEST: {
      auto* request = reinterpret_cast<xcb_selection_request_event_t*>(event);
      if (request->owner != window_) {
        return false;
      }
      HandleSelectionRequest(request);
      return true;
    }
    case XCB_SELECTION_CLEAR: {
      auto* clear = reinterpret_cast<xcb_selection_clear_event_t*>(event);
      if (clear->owner != window_) {
        return false;
      }
      if (clear->selection == display_->atom(X11Display::Atom::kClipboard)) {
        text_.reset();
      }
      return true;
    }
    case XCB_SELECTION_NOTIFY: {
      auto* notify = reinterpret_cast<xcb_selection_notify_event_t*>(event);
      if (notify->requestor != window_) {
        return false;
      }
      HandleSelectionNotify(notify);
      return true;
    }
    case XCB_PROPERTY_NOTIFY: {
      auto* notify = reinterpret_cast<xcb_property_notify_event_t*>(event);
      if (notify->window != window_) {
        return false;
      }
      HandlePropertyNotify(notify);
      return true;
    }
    default:
      return false;
  }
}

void X11Clipboard::HandleSelectionRequest(
    xcb_selection_request_event_t* event) {
  xcb_connection_t* connection = display_->connection();
  xcb_atom_t utf8_string = display_->atom(X11Display::Atom::kUtf8String);
  xcb_atom_t targets = display_->atom(X11Display::Atom::kTargets);

  // Obsolete clients don't specify a property.
  xcb_atom_t property =
      event->property != XCB_NONE ? event->property : event->target;
  bool provided = false;
  if (text_.has_value() &&
      event->selection == display_->atom(X11Display::Atom::kClipboard)) {
    if (event->target == targets) {
      xcb_atom_t supported[] = {targets, utf8_string, XCB_ATOM_STRING};
      xcb_change_property(connection, XCB_PROP_MODE_REPLACE, event->requestor,
                          property, XCB_ATOM_ATOM, 32,
                          sizeof(supported) / sizeof(supported[0]), supported);
      provided = true;
    } else if ((event->target == utf8_string ||
                event->target == XCB_ATOM_STRING) &&
               FitsInRequest(connection, text_->size())) {
      // Text too large for one request would need the INCR protocol, which
      // isn't supported, so the conversion is refused.
      xcb_change_property(connection, XCB_PROP_MODE_REPLACE, event->requestor,
                          property, event->target, 8, text_->size(),
                          text_->data());
      provided = true;
    }
  }

  xcb_selection_notify_event_t notify = {};
  notify.response_type = XCB_SELECTION_NOTIFY;
  notify.time = event->time;
  notify.requestor = event->requestor;
  notify.selection = event->selection;
  notify.target = event->target;
  notify.property = provided ? property : XCB_NONE;
  xcb_send_event(connection, false, event->requestor, XCB_EVENT_MASK_NO_EVENT,
                 reinterpret_cast<const char*>(&notify));
  display_->Flush();
}

void X11Clipboard::HandleSelectionNotify(xcb_selection_notify_event_t* event) {
  if (callbacks_.empty() || receiving_incr_ ||
      event->selection != display_->atom(X11Display::Atom::kClipboard)) {
    return;
  }
  if (event->property == XCB_NONE) {
    // Fall back to Latin-1 for old clients that don't support UTF-8.
    if (requested_target_ != XCB_ATOM_STRING) {
      requested_target_ = XCB_ATOM_STRING;
      xcb_convert_selection(
          display_->connection(), window_,
          display_->atom(X11Display::Atom::kClipboard), requested_target_,
          display_->atom(X11Display::Atom::kSelectionProperty),
          display_->last_input_time());
      display_->Flush();
      return;
    }
    FinishRequest(std::nullopt);
    return;
  }

  xcb_connection_t* connection = display_->connection();
  xcb_get_property_reply_t* reply = xcb_get_property_reply(
      connection,
      xcb_get_property(connection, true, window_, event->property,
                       XCB_GET_PROPERTY_TYPE_ANY, 0, UINT32_MAX / 4),
      nullptr);
  if (reply == nullptr) {
    FinishRequest(std::nullopt);
    return;
  }
  if (reply->type == display_->atom(X11Display::Atom::kIncr)) {
    // The contents are sent in parts, each time the property is deleted.
    receiving_incr_ = true;
    received_.clear();
    free(reply);
    display_->Flush();
    return;
  }
  std::string text(static_cast<const char*>(xcb_get_property_value(reply)),
                   xcb_get_property_value_length(reply));
  free(reply);
  display_->Flush();
  FinishRequest(std::move(text));
}

void X11Clipboard::HandlePropertyNotify(xcb_property_notify_event_t* event) {
  if (!receiving_incr_ || event->state != XCB_PROPERTY_NEW_VALUE ||
      event->atom != display_->atom(X11Display::Atom::kSelectionProperty)) {
    return;
  }
  xcb_connection_t* connection = display_->connection();
  xcb_get_property_reply_t* reply = xcb_get_property_reply(
      connection,
      xcb_get_property(connection, true, window_, event->atom,
                       XCB_GET_PROPERTY_TYPE_ANY, 0, UINT32_MAX / 4),
      nullptr);
  if (reply == nullptr) {
    FinishRequest(std::nullopt);
    return;
  }
  int length = xcb_get_property_value_length(reply);
  received_.append(static_cast<const char*>(xcb_get_property_value(reply)),
                   length);
  free(reply);
  display_->Flush();
  if (length == 0) {
    // An empty part marks the end.
    std::string text = std::move(received_);
    FinishRequest(std::move(text));
    return;
  }
  // Extend the timeout while parts keep arriving.
  uint64_t generation = ++request_generation_;
  display_->event_loop()->PostTask(
      [this, alive = std::weak_ptr<bool>(alive_), generation]() {
        if (!alive.expired() && generation == request_generation_ &&
            !callbacks_.empty()) {
          FML_LOG(WARNING) << "Timed out reading the clipboard";
          FinishRequest(std::nullopt);
        }
      },
      EventLoop::Clock::now() + kRequestTimeout);
}

}  // namespace flutter
