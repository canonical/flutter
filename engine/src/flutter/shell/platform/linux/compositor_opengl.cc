// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/compositor_opengl.h"

#include <poll.h>

#include <algorithm>
#include <cerrno>
#include <chrono>

#include "flutter/fml/eintr_wrapper.h"
#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/flutter_linux_view.h"

namespace flutter {

namespace {

// The number of buffers per view before waiting for the compositor to release
// one. This allows one buffer being rendered, one queued and one displayed,
// plus one spare for compositors that hold on to buffers longer.
constexpr size_t kMaxPoolSize = 4;

// How long to wait for the compositor to release a buffer before allocating
// another one.
constexpr int kBufferWaitTimeoutMs = 100;

}  // namespace

CompositorOpenGL::CompositorOpenGL(EGLManager* egl,
                                   WaylandDisplay* display,
                                   bool depth_stencil)
    : egl_(egl), display_(display), depth_stencil_(depth_stencil) {
  queue_ = wl_display_create_queue(display_->display());
  dmabuf_wrapper_ = static_cast<zwp_linux_dmabuf_v1*>(
      wl_proxy_create_wrapper(display_->dmabuf()));
  wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(dmabuf_wrapper_), queue_);
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

  // Submit the rendering commands. The compositor waits for them to complete
  // using the implicit fence on the dmabuf.
  glFlush();

  std::scoped_lock lock(views_mutex_);
  auto it = views_.find(view_id);
  if (it == views_.end()) {
    return false;
  }
  buffer->set_busy(true);
  it->second->PresentBuffer(buffer);
  return true;
}

CompositorOpenGL::PoolBuffer* CompositorOpenGL::GetBuffer(FlutterViewId view_id,
                                                          int width,
                                                          int height) {
  DispatchBufferEvents();

  auto& pool = pools_[view_id];
  TrimPool(pool, width, height);

  PoolBuffer* pool_buffer = FindFreeBuffer(pool, width, height);
  if (pool_buffer != nullptr) {
    return pool_buffer;
  }

  if (pool.size() >= kMaxPoolSize) {
    // Wait for the compositor to release a buffer.
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(kBufferWaitTimeoutMs);
    while (pool_buffer == nullptr) {
      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now());
      if (remaining.count() <= 0 || !WaitForBufferEvents(remaining.count())) {
        break;
      }
      pool_buffer = FindFreeBuffer(pool, width, height);
    }
    if (pool_buffer != nullptr) {
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
  pool.push_back(std::move(new_buffer));
  return pool.back().get();
}

void CompositorOpenGL::TrimPool(std::vector<std::unique_ptr<PoolBuffer>>& pool,
                                int width,
                                int height) {
  pool.erase(std::remove_if(pool.begin(), pool.end(),
                            [width, height](const auto& b) {
                              return !b->in_use && !b->buffer->busy() &&
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
    if (!b->in_use && !b->buffer->busy() && b->buffer->width() == width &&
        b->buffer->height() == height) {
      return b.get();
    }
  }
  return nullptr;
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
