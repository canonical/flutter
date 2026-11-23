// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_NEXT_WAYLAND_SHM_BUFFER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_NEXT_WAYLAND_SHM_BUFFER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "flutter/fml/macros.h"
#include "flutter/third_party/wayland_protocols/wayland-client-protocol.h"

namespace flutter {

// A buffer in shared memory that the compositor reads from. Pixels are 32 bit
// premultiplied ARGB in native byte order.
class ShmBuffer {
 public:
  // Creates a buffer of |width| x |height| pixels. |release_callback| is
  // called when the compositor stops reading from the buffer. Returns nullptr
  // on failure.
  static std::unique_ptr<ShmBuffer> Create(
      wl_shm* shm,
      int width,
      int height,
      std::function<void()> release_callback);

  ~ShmBuffer();

  int width() const { return width_; }
  int height() const { return height_; }
  int stride() const { return stride_; }
  uint8_t* data() const { return static_cast<uint8_t*>(data_); }
  wl_buffer* buffer() const { return buffer_; }

  // True from when the buffer is attached to a surface until the compositor
  // releases it. The contents must not be changed while busy.
  bool busy() const { return busy_; }
  void set_busy() { busy_ = true; }

 private:
  ShmBuffer(int width, int height, std::function<void()> release_callback);

  bool Initialize(wl_shm* shm);

  static void BufferRelease(void* data, wl_buffer* buffer);

  static const wl_buffer_listener kBufferListener;

  int width_;
  int height_;
  int stride_;
  size_t size_ = 0;
  void* data_ = nullptr;
  wl_buffer* buffer_ = nullptr;
  bool busy_ = false;
  std::function<void()> release_callback_;

  FML_DISALLOW_COPY_AND_ASSIGN(ShmBuffer);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_NEXT_WAYLAND_SHM_BUFFER_H_
