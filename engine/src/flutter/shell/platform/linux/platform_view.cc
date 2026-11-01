// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/platform_view.h"

#include <algorithm>
#include <limits>

#include "flutter/fml/logging.h"

namespace flutter {

LinuxPlatformView::Buffer::Buffer(std::shared_ptr<LinuxPlatformView> view,
                                  const DmabufFrame& frame)
    : view_(std::move(view)),
      width_(static_cast<int>(frame.width)),
      height_(static_cast<int>(frame.height)),
      release_(frame.release) {}

LinuxPlatformView::Buffer::~Buffer() {
  Remove();
}

void LinuxPlatformView::Buffer::Remove() {
  if (!removed_) {
    removed_ = true;
    view_->RemoveBuffer(this);
  }
}

LinuxPlatformView::LinuxPlatformView(int64_t id, EventLoop* event_loop)
    : id_(id), event_loop_(event_loop) {}

LinuxPlatformView::~LinuxPlatformView() {
  // Buffers hold a reference to the view, so they have all been freed.
  FML_DCHECK(buffers_.empty());
}

void LinuxPlatformView::SetCallbacks(ResizeCallback on_resize,
                                     PointerCallback on_pointer) {
  on_resize_ = std::move(on_resize);
  on_pointer_ = std::move(on_pointer);
  if (on_resize_ && shown_width_ > 0) {
    on_resize_(shown_width_, shown_height_, shown_scale_);
  }
}

std::unique_ptr<LinuxPlatformView::Buffer> LinuxPlatformView::CreateBuffer(
    const DmabufFrame& frame) {
  if (frame.width == 0 || frame.height == 0 || frame.planes.empty() ||
      frame.planes.size() > 4 ||
      frame.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
      frame.height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    FML_LOG(ERROR) << "Invalid platform view buffer";
    return nullptr;
  }
  std::unique_ptr<Buffer> buffer = CreateBackendBuffer(frame);
  if (buffer == nullptr) {
    return nullptr;
  }
  std::scoped_lock lock(mutex_);
  buffer->serial_ = next_serial_++;
  buffers_.push_back(buffer.get());
  return buffer;
}

void LinuxPlatformView::Present(Buffer* buffer) {
  if (buffer != nullptr && buffer->view_.get() != this) {
    FML_LOG(ERROR) << "Buffer presented in a different platform view";
    return;
  }
  Buffer* replaced = nullptr;
  {
    std::scoped_lock lock(mutex_);
    if (disposed_ || buffer == current_) {
      return;
    }
    replaced = current_;
    current_ = buffer;
    if (buffer != nullptr) {
      buffer->release_pending_ = true;
    }
    if (replaced != nullptr && !TakeRelease(replaced)) {
      replaced = nullptr;
    }
  }

  // The view can't be disposed while this runs, as that is also done on the
  // platform thread.
  OnPresent();
  if (replaced != nullptr && replaced->release_) {
    replaced->release_();
  }
}

void LinuxPlatformView::DispatchPointerEvent(
    const PlatformViewPointerEvent& event) {
  if (on_pointer_) {
    on_pointer_(event);
  }
}

void LinuxPlatformView::Dispose() {
  {
    std::scoped_lock lock(mutex_);
    disposed_ = true;
  }
  OnDispose();

  std::vector<std::function<void()>> released;
  {
    std::scoped_lock lock(mutex_);
    Buffer* current = current_;
    current_ = nullptr;
    if (current != nullptr && TakeRelease(current) && current->release_) {
      released.push_back(current->release_);
    }
  }
  // Release the buffers before clearing the callbacks, as that may destroy
  // the plugin's view and the buffers with it.
  for (const auto& release : released) {
    release();
  }
  on_resize_ = nullptr;
  on_pointer_ = nullptr;
}

void LinuxPlatformView::ReportSize(int width, int height, double scale) {
  if (width == reported_width_ && height == reported_height_ &&
      scale == reported_scale_) {
    return;
  }
  reported_width_ = width;
  reported_height_ = height;
  reported_scale_ = scale;
  event_loop_->PostTask([weak = weak_from_this(), width, height, scale]() {
    if (auto self = weak.lock()) {
      self->shown_width_ = width;
      self->shown_height_ = height;
      self->shown_scale_ = scale;
      if (self->on_resize_ && width > 0 && height > 0) {
        self->on_resize_(width, height, scale);
      }
    }
  });
}

void LinuxPlatformView::AddUse(Buffer* buffer) {
  buffer->uses_++;
}

std::function<void()> LinuxPlatformView::RemoveUse(Buffer* buffer) {
  if (buffer->uses_ > 0) {
    buffer->uses_--;
  }
  return TakeRelease(buffer) ? buffer->release_ : nullptr;
}

std::vector<std::function<void()>> LinuxPlatformView::RemoveAllUses() {
  std::vector<std::function<void()>> released;
  for (Buffer* buffer : buffers_) {
    buffer->uses_ = 0;
    if (TakeRelease(buffer) && buffer->release_) {
      released.push_back(buffer->release_);
    }
  }
  return released;
}

LinuxPlatformView::Buffer* LinuxPlatformView::FindBuffer(
    uint64_t serial) const {
  for (Buffer* buffer : buffers_) {
    if (buffer->serial_ == serial) {
      return buffer;
    }
  }
  return nullptr;
}

bool LinuxPlatformView::TakeRelease(Buffer* buffer) {
  if (!buffer->release_pending_ || buffer->uses_ > 0 || buffer == current_) {
    return false;
  }
  buffer->release_pending_ = false;
  return true;
}

void LinuxPlatformView::RemoveBuffer(Buffer* buffer) {
  std::scoped_lock lock(mutex_);
  buffers_.erase(std::remove(buffers_.begin(), buffers_.end(), buffer),
                 buffers_.end());
  if (current_ == buffer) {
    current_ = nullptr;
  }
  OnBufferRemoved(buffer);
}

}  // namespace flutter
