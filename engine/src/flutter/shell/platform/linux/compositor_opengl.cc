// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/compositor_opengl.h"

#include <poll.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <ctime>

#include "flutter/fml/eintr_wrapper.h"
#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/flutter_linux_view.h"
#include "flutter/shell/platform/linux/gpu_device.h"

namespace flutter {

namespace {

// The number of buffers per view before waiting for the compositor to release
// one. This allows one buffer being rendered, one queued and one displayed,
// plus one spare for compositors that hold on to buffers longer.
constexpr size_t kMaxPoolSize = 4;

// How long to wait for the compositor to release a buffer before allocating
// another one.
constexpr int kBufferWaitTimeoutMs = 100;

// How long to wait on the CPU for the compositor to finish reading from a
// released buffer.
constexpr int64_t kReleaseFenceTimeoutNs = 1000000000;

// Returns the current time on CLOCK_MONOTONIC, the clock DRM syncobj timeouts
// use.
int64_t NowNanoseconds() {
  timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<int64_t>(now.tv_sec) * 1000000000 + now.tv_nsec;
}

}  // namespace

CompositorOpenGL::CompositorOpenGL(EGLManager* egl,
                                   WaylandDisplay* display,
                                   bool depth_stencil)
    : egl_(egl), display_(display), depth_stencil_(depth_stencil) {
  queue_ = wl_display_create_queue(display_->display());
  dmabuf_wrapper_ = static_cast<zwp_linux_dmabuf_v1*>(
      wl_proxy_create_wrapper(display_->dmabuf()));
  wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(dmabuf_wrapper_), queue_);

  explicit_sync_ = display_->syncobj_manager() != nullptr &&
                   egl_->gpu()->supports_explicit_sync();
  FML_DLOG(INFO) << "Using " << (explicit_sync_ ? "explicit" : "implicit")
                 << " synchronization";
}

CompositorOpenGL::~CompositorOpenGL() {
  // The engine has shut down, so the raster thread is no longer using the
  // render context and it can be used to free the buffers.
  bool made_current = egl_->MakeRenderContextCurrent();
  pools_.clear();
  if (made_current) {
    egl_->ClearCurrent();
  }

  wl_proxy_wrapper_destroy(dmabuf_wrapper_);
  wl_event_queue_destroy(queue_);
}

void CompositorOpenGL::AddView(FlutterViewId view_id, FlutterLinuxView* view) {
  std::scoped_lock lock(views_mutex_);
  views_[view_id] = view;
}

void CompositorOpenGL::RemoveView(FlutterViewId view_id) {
  std::scoped_lock lock(views_mutex_);
  views_.erase(view_id);
}

bool CompositorOpenGL::CreateBackingStore(
    const FlutterBackingStoreConfig& config,
    FlutterBackingStore* result) {
  int width = static_cast<int>(config.size.width);
  int height = static_cast<int>(config.size.height);
  if (width <= 0 || height <= 0) {
    return false;
  }

  PoolBuffer* pool_buffer = GetBuffer(config.view_id, width, height);
  if (pool_buffer == nullptr) {
    return false;
  }
  pool_buffer->in_use = true;

  result->user_data = pool_buffer;
  result->type = kFlutterBackingStoreTypeOpenGL;
  result->open_gl.type = kFlutterOpenGLTargetTypeFramebuffer;
  result->open_gl.framebuffer.target = DmabufBuffer::kGLFormat;
  result->open_gl.framebuffer.name = pool_buffer->buffer->framebuffer();
  result->open_gl.framebuffer.user_data = nullptr;
  result->open_gl.framebuffer.destruction_callback = [](void* user_data) {};
  return true;
}

bool CompositorOpenGL::CollectBackingStore(const FlutterBackingStore* store) {
  // The buffer stays in the pool for reuse once the compositor releases it.
  static_cast<PoolBuffer*>(store->user_data)->in_use = false;
  return true;
}

bool CompositorOpenGL::Present(FlutterViewId view_id,
                               const FlutterLayer** layers,
                               size_t layers_count) {
  if (layers_count == 0) {
    return true;
  }

  // Only a single layer is currently supported. Multiple layers will be
  // presented using subsurfaces.
  if (layers_count > 1 && !warned_multiple_layers_) {
    FML_LOG(WARNING) << "Platform views are not supported, only the first "
                        "layer will be shown";
    warned_multiple_layers_ = true;
  }
  const FlutterLayer* layer = layers[0];
  if (layer->type != kFlutterLayerContentTypeBackingStore) {
    return false;
  }
  auto* pool_buffer = static_cast<PoolBuffer*>(layer->backing_store->user_data);
  DmabufBuffer* buffer = pool_buffer->buffer.get();

  std::scoped_lock lock(views_mutex_);
  auto it = views_.find(view_id);
  if (it == views_.end()) {
    return false;
  }

  if (pool_buffer->timeline == nullptr) {
    // Submit the rendering commands. The compositor waits for them to
    // complete using the implicit fence on the dmabuf.
    glFlush();
    buffer->set_busy(true);
    it->second->PresentBuffer(buffer, nullptr);
    return true;
  }

  DrmTimeline* timeline = pool_buffer->timeline.get();
  WaylandWindow::SyncPoints sync = {
      .timeline = timeline->timeline(),
      .acquire_point = ++pool_buffer->last_point,
      .release_point = ++pool_buffer->last_point,
  };
  if (!SignalWhenRenderingComplete(timeline, sync.acquire_point)) {
    return false;
  }
  pool_buffer->release_point = sync.release_point;
  it->second->PresentBuffer(buffer, &sync);
  return true;
}

bool CompositorOpenGL::SignalWhenRenderingComplete(DrmTimeline* timeline,
                                                   uint64_t point) {
  GpuDevice* gpu = egl_->gpu();
  const EGLint attributes[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID,
                               EGL_NO_NATIVE_FENCE_FD_ANDROID, EGL_NONE};
  EGLSyncKHR sync = gpu->CreateSync(gpu->display(),
                                    EGL_SYNC_NATIVE_FENCE_ANDROID, attributes);
  // The fence is only created once the commands are submitted.
  glFlush();
  if (sync != EGL_NO_SYNC_KHR) {
    fml::UniqueFD fence(gpu->DupNativeFenceFD(gpu->display(), sync));
    gpu->DestroySync(gpu->display(), sync);
    if (fence.is_valid() && timeline->ImportSyncFile(point, fence)) {
      return true;
    }
  }

  FML_DLOG(WARNING) << "Unable to create fence, waiting for rendering";
  glFinish();
  return timeline->Signal(point);
}

CompositorOpenGL::PoolBuffer* CompositorOpenGL::GetBuffer(FlutterViewId view_id,
                                                          int width,
                                                          int height) {
  DispatchBufferEvents();

  auto& pool = pools_[view_id];
  TrimPool(pool, width, height);

  PoolBuffer* pool_buffer = FindFreeBuffer(pool, width, height);
  if (pool_buffer != nullptr) {
    WaitForRelease(pool_buffer);
    return pool_buffer;
  }

  if (pool.size() >= kMaxPoolSize) {
    // Wait for the compositor to release a buffer.
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(kBufferWaitTimeoutMs);
    while (pool_buffer == nullptr) {
      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now());
      if (remaining.count() <= 0 ||
          !WaitForReleasedBuffer(pool, remaining.count())) {
        break;
      }
      pool_buffer = FindFreeBuffer(pool, width, height);
    }
    if (pool_buffer != nullptr) {
      WaitForRelease(pool_buffer);
      return pool_buffer;
    }
    FML_DLOG(WARNING) << "Timed out waiting for compositor to release buffer";
  }

  // Ensure the buffers are created in the render context.
  if (!egl_->IsRenderContextCurrent() && !egl_->MakeRenderContextCurrent()) {
    return nullptr;
  }
  std::unique_ptr<DmabufBuffer> buffer = DmabufBuffer::Create(
      egl_, dmabuf_wrapper_, display_->GetModifiers(DmabufBuffer::kFormat),
      width, height, depth_stencil_);
  if (!buffer) {
    return nullptr;
  }
  auto new_buffer = std::make_unique<PoolBuffer>();
  new_buffer->buffer = std::move(buffer);
  if (explicit_sync_) {
    new_buffer->timeline =
        DrmTimeline::Create(egl_->gpu()->drm_fd(), display_->syncobj_manager());
    if (!new_buffer->timeline) {
      return nullptr;
    }
  }
  pool.push_back(std::move(new_buffer));
  return pool.back().get();
}

void CompositorOpenGL::TrimPool(std::vector<std::unique_ptr<PoolBuffer>>& pool,
                                int width,
                                int height) {
  pool.erase(std::remove_if(pool.begin(), pool.end(),
                            [this, width, height](const auto& b) {
                              return IsFree(b.get()) &&
                                     (b->buffer->width() != width ||
                                      b->buffer->height() != height);
                            }),
             pool.end());
}

CompositorOpenGL::PoolBuffer* CompositorOpenGL::FindFreeBuffer(
    std::vector<std::unique_ptr<PoolBuffer>>& pool,
    int width,
    int height) {
  for (auto& b : pool) {
    if (IsFree(b.get()) && b->buffer->width() == width &&
        b->buffer->height() == height) {
      return b.get();
    }
  }
  return nullptr;
}

bool CompositorOpenGL::IsReleased(const PoolBuffer* pool_buffer) const {
  if (pool_buffer->timeline == nullptr) {
    return !pool_buffer->buffer->busy();
  }
  return pool_buffer->release_point == 0 ||
         pool_buffer->timeline->IsAvailable(pool_buffer->release_point);
}

bool CompositorOpenGL::IsFree(const PoolBuffer* pool_buffer) const {
  return !pool_buffer->in_use && IsReleased(pool_buffer);
}

void CompositorOpenGL::WaitForRelease(PoolBuffer* pool_buffer) {
  if (pool_buffer->timeline == nullptr || pool_buffer->release_point == 0) {
    return;
  }
  DrmTimeline* timeline = pool_buffer->timeline.get();
  uint64_t point = pool_buffer->release_point;
  pool_buffer->release_point = 0;

  // Have the GPU wait for the compositor's reads to complete before
  // rendering, so the CPU doesn't block.
  GpuDevice* gpu = egl_->gpu();
  if (gpu->WaitSync != nullptr && egl_->IsRenderContextCurrent()) {
    fml::UniqueFD fence = timeline->ExportSyncFile(point);
    if (fence.is_valid()) {
      const EGLint attributes[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID,
                                   fence.get(), EGL_NONE};
      EGLSyncKHR sync = gpu->CreateSync(
          gpu->display(), EGL_SYNC_NATIVE_FENCE_ANDROID, attributes);
      if (sync != EGL_NO_SYNC_KHR) {
        // EGL owns the fence now.
        [[maybe_unused]] int fd = fence.release();
        bool waited = gpu->WaitSync(gpu->display(), sync, 0) == EGL_TRUE;
        gpu->DestroySync(gpu->display(), sync);
        if (waited) {
          return;
        }
      }
    }
  }

  if (!timeline->Wait(point, NowNanoseconds() + kReleaseFenceTimeoutNs)) {
    FML_DLOG(WARNING) << "Timed out waiting for compositor to finish with "
                         "buffer";
  }
}

bool CompositorOpenGL::WaitForReleasedBuffer(
    std::vector<std::unique_ptr<PoolBuffer>>& pool,
    int timeout_ms) {
  if (!explicit_sync_) {
    return WaitForBufferEvents(timeout_ms);
  }

  std::vector<DrmTimeline*> timelines;
  std::vector<uint64_t> points;
  for (const auto& b : pool) {
    if (!b->in_use && b->release_point != 0) {
      timelines.push_back(b->timeline.get());
      points.push_back(b->release_point);
    }
  }
  return DrmTimeline::WaitAnyAvailable(
      timelines.data(), points.data(), timelines.size(),
      NowNanoseconds() + static_cast<int64_t>(timeout_ms) * 1000000);
}

void CompositorOpenGL::DispatchBufferEvents() {
  wl_display_dispatch_queue_pending(display_->display(), queue_);
}

bool CompositorOpenGL::WaitForBufferEvents(int timeout_ms) {
  wl_display* display = display_->display();
  while (wl_display_prepare_read_queue(display, queue_) != 0) {
    if (wl_display_dispatch_queue_pending(display, queue_) < 0) {
      return false;
    }
  }
  wl_display_flush(display);

  pollfd fd = {.fd = wl_display_get_fd(display), .events = POLLIN};
  int result = FML_HANDLE_EINTR(poll(&fd, 1, timeout_ms));
  if (result <= 0) {
    wl_display_cancel_read(display);
    return false;
  }
  if (wl_display_read_events(display) < 0) {
    return false;
  }
  return wl_display_dispatch_queue_pending(display, queue_) >= 0;
}

}  // namespace flutter
