// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DISPLAY_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DISPLAY_H_

#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/clipboard.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/window.h"

namespace flutter {

class DecorationRenderer;

// A connection to a display server, which shows windows and provides input.
// Implemented by each display backend (Wayland, X11 or KMS).
//
// Events are dispatched on the event loop thread when this is added as a
// source to the loop.
class Display : public EventLoop::Source {
 public:
  enum class Backend {
    kWayland,
    kX11,
    kKms,
  };

  // A monitor connected to the display server.
  struct Output {
    // Unique for the lifetime of the connection.
    uint32_t id = 0;
    // The size in physical pixels, as shown on the screen.
    int32_t width = 0;
    int32_t height = 0;
    // The refresh rate in frames per second, or zero if unknown.
    double refresh_rate = 0;
    // The number of physical pixels per logical pixel.
    double pixel_ratio = 1;
  };

  // Notified when outputs are added, removed or changed.
  class OutputObserver {
   public:
    virtual ~OutputObserver() = default;
    virtual void OnOutputsChanged() = 0;
  };

  // Connects to the display server specified by the environment. Wayland is
  // used if WAYLAND_DISPLAY is set, otherwise X11 if DISPLAY is set, otherwise
  // the screen is used directly with KMS. The FLUTTER_LINUX_BACKEND environment
  // variable may be set to "wayland", "x11" or "kms" to choose one. Returns
  // nullptr if unable to connect.
  //
  // |event_loop| is the loop the display is dispatched from, and must
  // outlive it.
  static std::unique_ptr<Display> Create(EventLoop* event_loop);

  ~Display() override = default;

  virtual Backend backend() const = 0;

  // The clipboard, or nullptr if the display server doesn't support one.
  virtual Clipboard* clipboard() const = 0;

  // The outputs whose properties are known.
  virtual std::vector<Output> GetOutputs() const = 0;

  // The device the display server prefers buffers to be allocated on, if
  // known.
  virtual std::optional<dev_t> main_device() const = 0;

  // Sets the cursor shown over the window the pointer is in. |shape| is a
  // wp_cursor_shape_device_v1 shape, or kHiddenCursorShape.
  virtual void SetCursor(uint32_t shape) = 0;

  // Rings the bell for |window|, which may be nullptr.
  virtual void RingBell(Window* window) = 0;

  // Creates a window. This blocks until the window is configured, without
  // dispatching events for other windows. Decorations are drawn with
  // |decoration_renderer| if it is not nullptr and the backend draws its
  // own. Returns nullptr on failure.
  virtual std::unique_ptr<Window> CreateWindow(
      DecorationRenderer* decoration_renderer,
      Window::Delegate* delegate,
      const Window::Params& params) = 0;

  // Adds or removes an observer, which must be removed before it is
  // destroyed.
  void AddOutputObserver(OutputObserver* observer);
  void RemoveOutputObserver(OutputObserver* observer);

  // Sets a callback that is called if the connection to the display server
  // fails.
  void SetErrorCallback(std::function<void()> callback) {
    error_callback_ = std::move(callback);
  }

 protected:
  Display() = default;

  // Notifies observers of a change to the outputs.
  void NotifyOutputsChanged();

  // Calls the error callback, if set.
  void NotifyError();

 private:
  std::vector<OutputObserver*> output_observers_;
  std::function<void()> error_callback_;

  FML_DISALLOW_COPY_AND_ASSIGN(Display);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DISPLAY_H_
