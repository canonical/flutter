// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_next/wayland/shm_buffer.h"

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "flutter/fml/eintr_wrapper.h"
#include "flutter/fml/logging.h"
#include "flutter/fml/unique_fd.h"

namespace flutter {

namespace {

constexpr int kBytesPerPixel = 4;

}  // namespace

const wl_buffer_listener ShmBuffer::kBufferListener = {
    .release = BufferRelease,
};

std::unique_ptr<ShmBuffer> ShmBuffer::Create(
    wl_shm* shm,
    int width,
    int height,
    std::function<void()> release_callback) {
  std::unique_ptr<ShmBuffer> buffer(
      new ShmBuffer(width, height, std::move(release_callback)));
  if (!buffer->Initialize(shm)) {
    return nullptr;
  }
  return buffer;
}

ShmBuffer::ShmBuffer(int width,
                     int height,
                     std::function<void()> release_callback)
    : width_(width),
      height_(height),
      stride_(width * kBytesPerPixel),
      release_callback_(std::move(release_callback)) {}

ShmBuffer::~ShmBuffer() {
  if (buffer_ != nullptr) {
    wl_buffer_destroy(buffer_);
  }
  if (data_ != nullptr) {
    munmap(data_, size_);
  }
}

bool ShmBuffer::Initialize(wl_shm* shm) {
  size_ = static_cast<size_t>(stride_) * height_;

  fml::UniqueFD fd(memfd_create("flutter-shm-buffer", MFD_CLOEXEC));
  if (!fd.is_valid()) {
    FML_LOG(ERROR) << "Failed to create shared memory: " << strerror(errno);
    return false;
  }
  if (FML_HANDLE_EINTR(ftruncate(fd.get(), size_)) != 0) {
    FML_LOG(ERROR) << "Failed to size shared memory: " << strerror(errno);
    return false;
  }
  void* data =
      mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
  if (data == MAP_FAILED) {
    FML_LOG(ERROR) << "Failed to map shared memory: " << strerror(errno);
    return false;
  }
  data_ = data;

  wl_shm_pool* pool = wl_shm_create_pool(shm, fd.get(), size_);
  buffer_ = wl_shm_pool_create_buffer(pool, 0, width_, height_, stride_,
                                      WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  wl_buffer_add_listener(buffer_, &kBufferListener, this);
  return true;
}

void ShmBuffer::BufferRelease(void* data, wl_buffer* buffer) {
  auto* self = static_cast<ShmBuffer*>(data);
  self->busy_ = false;
  // The callback may destroy this buffer, so call a copy.
  auto callback = self->release_callback_;
  if (callback) {
    callback();
  }
}

}  // namespace flutter
