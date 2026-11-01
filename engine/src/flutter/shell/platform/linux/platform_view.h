// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_VIEW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_VIEW_H_

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/external_texture.h"

namespace flutter {

// A pointer event in a platform view, forwarded from the framework.
struct PlatformViewPointerEvent {
  enum class Kind {
    kDown,
    kMove,
    kUp,
    kCancel,
    kHover,
    kExit,
    kScroll,
  };
  enum class DeviceKind {
    kMouse,
    kTouch,
    kStylus,
    kTrackpad,
  };

  Kind kind = Kind::kMove;
  DeviceKind device_kind = DeviceKind::kMouse;
  int32_t device = 0;
  int64_t buttons = 0;

  // The position in logical pixels, relative to the view's top left.
  double x = 0;
  double y = 0;

  double scroll_delta_x = 0;
  double scroll_delta_y = 0;

  int64_t timestamp_us = 0;
};

// A native view shown in a Flutter view, such as a video or web page.
//
// The plugin providing the view creates buffers from dmabufs and presents
// them, and the display backend shows the latest buffer where the Flutter
// scene places the view. This class tracks the buffers so each is released
// back to the plugin once it has been replaced and the backend has finished
// with it; subclasses show them.
//
// Plugins call the public methods on the platform thread.
class LinuxPlatformView
    : public std::enable_shared_from_this<LinuxPlatformView> {
 public:
  // A buffer that can be presented in the view, created from a dmabuf.
  class Buffer {
   public:
    virtual ~Buffer();

    int width() const { return width_; }
    int height() const { return height_; }

    // Identifies the buffer while it exists, unlike its address which may
    // be reused.
    uint64_t serial() const { return serial_; }

   protected:
    Buffer(std::shared_ptr<LinuxPlatformView> view, const DmabufFrame& frame);

    // Stops the view using the buffer. Subclasses call this before freeing
    // anything the view may read from another thread.
    void Remove();

   private:
    friend class LinuxPlatformView;

    std::shared_ptr<LinuxPlatformView> view_;
    int width_;
    int height_;
    uint64_t serial_ = 0;
    std::function<void()> release_;

    // The number of times the backend is using the buffer. Guarded by the
    // view's mutex.
    int uses_ = 0;

    // True if the plugin is waiting for the buffer to be released. Guarded
    // by the view's mutex.
    bool release_pending_ = false;

    bool removed_ = false;

    FML_DISALLOW_COPY_AND_ASSIGN(Buffer);
  };

  // Called on the platform thread when the size the view is shown at
  // changes, in physical pixels, with the number of physical pixels per
  // logical pixel.
  using ResizeCallback =
      std::function<void(int width, int height, double scale)>;
  using PointerCallback = std::function<void(const PlatformViewPointerEvent&)>;

  virtual ~LinuxPlatformView();

  int64_t id() const { return id_; }

  void SetCallbacks(ResizeCallback on_resize, PointerCallback on_pointer);

  // Creates a buffer from |frame|. The planes' file descriptors are not
  // kept. |frame.release| is called on the platform thread once the buffer
  // has been replaced by another one and the backend has finished with it.
  // Returns nullptr if the buffer can't be shown.
  std::unique_ptr<Buffer> CreateBuffer(const DmabufFrame& frame);

  // Shows |buffer| in the view. Replaces the previous buffer, which is
  // released once the backend has finished with it. The buffer is shown
  // immediately if the view is visible, otherwise when it is next drawn.
  void Present(Buffer* buffer);

  // Forwards a pointer event from the framework to the plugin.
  void DispatchPointerEvent(const PlatformViewPointerEvent& event);

  // Removes the view. Called when the framework disposes it. Buffers may
  // remain until the plugin frees them.
  void Dispose();

 protected:
  LinuxPlatformView(int64_t id, EventLoop* event_loop);

  // Creates a buffer for this backend from |frame|, which has been checked
  // to be valid.
  virtual std::unique_ptr<Buffer> CreateBackendBuffer(
      const DmabufFrame& frame) = 0;

  // Called on the platform thread when |current_| changes, to show it.
  virtual void OnPresent() = 0;

  // Called on the platform thread when the view is disposed, before
  // released buffers are returned to the plugin.
  virtual void OnDispose() {}

  // Called with |mutex_| held when |buffer| is freed.
  virtual void OnBufferRemoved(Buffer* buffer) {}

  // Tells the plugin the view is shown at |width| x |height| physical pixels
  // with |scale| physical pixels per logical pixel, if that has changed.
  // Called with |mutex_| held from any thread.
  void ReportSize(int width, int height, double scale);

  // Marks |buffer| as used by the backend, so it isn't released. Called with
  // |mutex_| held.
  void AddUse(Buffer* buffer);

  // Ends a use of |buffer| added with AddUse, releasing the buffer if it has
  // been replaced and is no longer used. Called with |mutex_| held. Returns
  // the release callback to call once the lock is dropped, or nullptr.
  std::function<void()> RemoveUse(Buffer* buffer);

  // Ends all uses of the buffers, for when the backend no longer shows them.
  // Called with |mutex_| held. Returns the release callbacks to call once
  // the lock is dropped.
  std::vector<std::function<void()>> RemoveAllUses();

  // True if |buffer| is used by the backend. Called with |mutex_| held.
  static bool InUse(const Buffer* buffer) { return buffer->uses_ > 0; }

  // Returns the buffer with |serial|, or nullptr if it has been freed.
  // Called with |mutex_| held.
  Buffer* FindBuffer(uint64_t serial) const;

  // Guards the view's state, which the backend reads while drawing.
  std::mutex mutex_;

  // True once disposed, after which the view isn't shown.
  bool disposed_ = false;

  // The buffer the plugin last presented, or nullptr.
  Buffer* current_ = nullptr;

 private:
  // True if |buffer| has been replaced and the backend has finished with
  // it, so its release callback should be called. Called with |mutex_| held.
  bool TakeRelease(Buffer* buffer);

  // Called when a buffer is freed by the plugin.
  void RemoveBuffer(Buffer* buffer);

  int64_t id_;
  EventLoop* event_loop_;

  ResizeCallback on_resize_;
  PointerCallback on_pointer_;

  // Buffers that exist, so the release of replaced buffers can be tracked.
  // Guarded by |mutex_|.
  std::vector<Buffer*> buffers_;
  uint64_t next_serial_ = 1;

  // The size last sent to the platform thread, guarded by |mutex_|.
  int reported_width_ = 0;
  int reported_height_ = 0;
  double reported_scale_ = 0;

  // The size the view is shown at, only used on the platform thread.
  int shown_width_ = 0;
  int shown_height_ = 0;
  double shown_scale_ = 0;

  FML_DISALLOW_COPY_AND_ASSIGN(LinuxPlatformView);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_VIEW_H_
