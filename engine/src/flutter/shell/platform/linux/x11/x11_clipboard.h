// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_X11_X11_CLIPBOARD_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_X11_X11_CLIPBOARD_H_

#include <xcb/xcb.h>

#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/clipboard.h"

namespace flutter {

class X11Display;

// The CLIPBOARD selection of an X11 server.
class X11Clipboard : public Clipboard {
 public:
  // Returns nullptr on failure. |display| must outlive the clipboard.
  static std::unique_ptr<X11Clipboard> Create(X11Display* display);

  ~X11Clipboard() override;

  // The window used to own and receive selections.
  xcb_window_t window() const { return window_; }

  // Processes an event for the clipboard's window. Returns true if the event
  // was handled.
  bool HandleEvent(xcb_generic_event_t* event);

  // |Clipboard|
  bool SetText(const std::string& text) override;

  // |Clipboard|
  void GetText(TextCallback callback) override;

  // |Clipboard|
  bool HasText() const override;

 private:
  explicit X11Clipboard(X11Display* display);

  bool Initialize();

  // Requests the clipboard contents for the first queued callback.
  void RequestText();

  // Calls the first queued callback with |text| and requests the next.
  void FinishRequest(std::optional<std::string> text);

  // Answers another client's request for the clipboard contents.
  void HandleSelectionRequest(xcb_selection_request_event_t* event);

  // Handles the clipboard owner providing the requested contents.
  void HandleSelectionNotify(xcb_selection_notify_event_t* event);

  // Reads the next part of contents sent incrementally.
  void HandlePropertyNotify(xcb_property_notify_event_t* event);

  X11Display* display_;
  xcb_window_t window_ = XCB_NONE;

  // The text this application placed on the clipboard, if it still owns it.
  std::optional<std::string> text_;

  // Callbacks waiting for the clipboard contents. The first is being
  // requested.
  std::deque<TextCallback> callbacks_;
  uint64_t request_generation_ = 0;
  xcb_atom_t requested_target_ = XCB_NONE;

  // The contents being received incrementally (INCR).
  bool receiving_incr_ = false;
  std::string received_;

  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(X11Clipboard);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_X11_X11_CLIPBOARD_H_
