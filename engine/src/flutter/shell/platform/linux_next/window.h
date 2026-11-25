// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_NEXT_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_NEXT_WINDOW_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace flutter {

// A top-level window showing Flutter content. Implemented by each display
// backend.
//
// Must be used on the platform thread.
class Window {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when the window size or scale changes.
    virtual void OnWindowConfigure() = 0;

    // Called when the user requests the window be closed.
    virtual void OnWindowClose() = 0;
  };

  struct Params {
    // The initial logical size of the content.
    int width = 0;
    int height = 0;
    std::string application_id;
  };

  virtual ~Window() = default;

  // The size of the content in logical pixels.
  virtual int width() const = 0;
  virtual int height() const = 0;

  // The number of physical pixels per logical pixel.
  virtual double scale() const = 0;

  // The size of the content in physical pixels.
  int physical_width() const { return width() * scale(); }
  int physical_height() const { return height() * scale(); }

  virtual void SetTitle(const std::string& title) = 0;

  // Shows a frame of |width| x |height| physical pixels, with rows
  // |row_bytes| apart. Pixels are 32 bit premultiplied ARGB in native byte
  // order.
  virtual void Present(const uint8_t* pixels,
                       int width,
                       int height,
                       size_t row_bytes) = 0;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_NEXT_WINDOW_H_
