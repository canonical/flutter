// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/compositor_opengl.h"

#include <drm_fourcc.h>
#include <poll.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "flutter/fml/eintr_wrapper.h"
#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/flutter_linux_view.h"
#include "flutter/shell/platform/linux/gpu_device.h"
#include "flutter/shell/platform/linux/texture_buffer.h"
#include "flutter/shell/platform/linux/wayland/dmabuf_buffer.h"
#include "flutter/shell/platform/linux/wayland/shm_buffer.h"

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

// Warns that frames are being copied into shared memory, once per process.
void WarnShmFallback(const char* reason) {
  static std::once_flag once;
  std::call_once(once, [reason]() {
    fprintf(stderr,
            "Flutter: linux_dmabuf is not available (%s), falling back to "
            "wl_shm. Frames will be copied from the GPU, which reduces "
            "performance.\n",
            reason);
  });
}

}  // namespace

CompositorOpenGL::CompositorOpenGL(EGLManager* egl,
                                   Display* display,
                                   bool depth_stencil)
    : egl_(egl),
      display_(display->backend() == Display::Backend::kWayland
                   ? static_cast<WaylandDisplay*>(display)
                   : nullptr),
      depth_stencil_(depth_stencil),
      use_dmabuf_(false),
      explicit_sync_(false),
      buffer_modifier_(DRM_FORMAT_MOD_INVALID) {
  if (display_ == nullptr) {
    return;
  }
  queue_ = wl_display_create_queue(display_->display());
  shm_wrapper_ = static_cast<wl_shm*>(wl_proxy_create_wrapper(display_->shm()));
  wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(shm_wrapper_), queue_);

  if (display_->dmabuf() == nullptr) {
    WarnShmFallback("the Wayland compositor does not support it");
    return;
  }
  if (!egl_->gpu()->supports_dmabuf_import()) {
    WarnShmFallback("EGL does not support EGL_EXT_image_dma_buf_import");
    return;
  }
  if (display_->GetModifiers(DmabufBuffer::kFormat).empty()) {
    WarnShmFallback("the Wayland compositor does not support ARGB8888");
    return;
  }
  use_dmabuf_ = true;
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
  corner_mask_.reset();
  if (made_current) {
    egl_->ClearCurrent();
  }

  if (dmabuf_wrapper_ != nullptr) {
    wl_proxy_wrapper_destroy(dmabuf_wrapper_);
  }
  if (shm_wrapper_ != nullptr) {
    wl_proxy_wrapper_destroy(shm_wrapper_);
  }
  if (queue_ != nullptr) {
    wl_event_queue_destroy(queue_);
  }
}

void CompositorOpenGL::AddView(FlutterViewId view_id, FlutterLinuxView* view) {
  std::scoped_lock lock(views_mutex_);
  views_[view_id] = view;
}

void CompositorOpenGL::RemoveView(FlutterViewId view_id) {
  std::unique_lock<std::mutex> lock(views_mutex_);
  views_.erase(view_id);
  presented_condition_.wait(
      lock, [this, view_id]() { return presenting_view_ != view_id; });
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
  result->open_gl.framebuffer.target = RenderBuffer::kGLFormat;
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
  // The engine always draws the bottom layer into a backing store, which is
  // shown in the window's surface.
  if (layers[0]->type != kFlutterLayerContentTypeBackingStore) {
    FML_LOG(ERROR) << "The bottom layer must be a backing store";
    return false;
  }

  size_t n_backing_stores = 0;
  for (size_t i = 0; i < layers_count; i++) {
    if (layers[i]->type == kFlutterLayerContentTypeBackingStore) {
      n_backing_stores++;
    }
  }
  backing_stores_per_frame_[view_id] = n_backing_stores;

  FlutterLinuxView* view;
  {
    std::scoped_lock lock(views_mutex_);
    auto it = views_.find(view_id);
    if (it == views_.end()) {
      return false;
    }
    view = it->second;
    presenting_view_ = view_id;
  }
  bool result = PresentToView(view, layers, layers_count);
  {
    std::scoped_lock lock(views_mutex_);
    presenting_view_.reset();
  }
  presented_condition_.notify_all();
  return result;
}

void CompositorOpenGL::SetPlatformViewLookup(PlatformViewLookup lookup) {
  platform_view_lookup_ = std::move(lookup);
}

bool CompositorOpenGL::PresentToView(FlutterLinuxView* view,
                                     const FlutterLayer** layers,
                                     size_t layers_count) {
  auto* root = static_cast<PoolBuffer*>(layers[0]->backing_store->user_data);
  RenderBuffer* root_buffer = root->buffer.get();
  int width = root_buffer->width();
  int height = root_buffer->height();
  double corner_radius = view->window()->ContentCornerRadius(width, height);
  if (corner_radius > 0) {
    if (!corner_mask_) {
      corner_mask_ = std::make_unique<CornerMask>();
    }
    corner_mask_->Apply(root_buffer->framebuffer(), width, height,
                        corner_radius);
  }

  // The buffers to show, the root first.
  std::vector<PoolBuffer*> buffers = {root};
  std::vector<Window::Layer> window_layers;
  double scale = view->window()->scale();
  for (size_t i = 1; i < layers_count; i++) {
    const FlutterLayer* layer = layers[i];
    if (layer->type == kFlutterLayerContentTypePlatformView) {
      std::shared_ptr<LinuxPlatformView> platform_view =
          platform_view_lookup_
              ? platform_view_lookup_(layer->platform_view->identifier)
              : nullptr;
      if (platform_view == nullptr) {
        continue;
      }
      Window::Layer window_layer;
      window_layer.platform_view = std::move(platform_view);
      window_layer.geometry = GetPlatformViewGeometry(*layer, scale);
      window_layers.push_back(std::move(window_layer));
      continue;
    }

    auto* pool_buffer =
        static_cast<PoolBuffer*>(layer->backing_store->user_data);
    RenderBuffer* buffer = pool_buffer->buffer.get();
    PixelRect content =
        GetPaintBounds(layer, buffer->width(), buffer->height());
    if (content.empty() || buffer->width() != width ||
        buffer->height() != height) {
      continue;
    }
    Window::Layer window_layer;
    window_layer.buffer = buffer;
    window_layer.content = content;
    window_layers.push_back(std::move(window_layer));
    buffers.push_back(pool_buffer);
  }

  for (PoolBuffer* pool_buffer : buffers) {
    if (!pool_buffer->buffer->PrepareToPresent()) {
      return false;
    }
  }

  if (root->timeline == nullptr) {
    // Submit the rendering commands. The compositor waits for them to
    // complete using the implicit fences on the dmabufs.
    glFlush();
    // Other backends copy the buffers while presenting, so they are never
    // held by the display server.
    if (display_ != nullptr) {
      for (PoolBuffer* pool_buffer : buffers) {
        pool_buffer->buffer->set_busy(true);
      }
    }
    view->PresentBuffer(root_buffer, nullptr, window_layers);
    return true;
  }

  std::vector<Window::SyncPoints> sync(buffers.size());
  for (size_t i = 0; i < buffers.size(); i++) {
    PoolBuffer* pool_buffer = buffers[i];
    sync[i] = {
        .timeline = pool_buffer->timeline.get(),
        .acquire_point = ++pool_buffer->last_point,
        .release_point = ++pool_buffer->last_point,
    };
  }
  if (!SignalWhenRenderingComplete(buffers, sync)) {
    return false;
  }
  for (size_t i = 0; i < buffers.size(); i++) {
    buffers[i]->release_point = sync[i].release_point;
  }
  size_t overlay = 1;
  for (Window::Layer& window_layer : window_layers) {
    if (window_layer.buffer != nullptr) {
      window_layer.sync = sync[overlay++];
    }
  }
  view->PresentBuffer(root_buffer, &sync[0], window_layers);
  return true;
}

PixelRect CompositorOpenGL::GetPaintBounds(const FlutterLayer* layer,
                                           int width,
                                           int height) {
  PixelRect full = {0, 0, static_cast<double>(width),
                    static_cast<double>(height)};
  const FlutterBackingStorePresentInfo* info =
      layer->backing_store_present_info;
  if (info == nullptr || info->paint_region == nullptr) {
    return full;
  }
  const FlutterRegion* region = info->paint_region;
  double left = INFINITY, top = INFINITY;
  double right = -INFINITY, bottom = -INFINITY;
  for (size_t i = 0; i < region->rects_count; i++) {
    const FlutterRect& rect = region->rects[i];
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
      continue;
    }
    left = std::min(left, rect.left);
    top = std::min(top, rect.top);
    right = std::max(right, rect.right);
    bottom = std::max(bottom, rect.bottom);
  }
  if (right <= left || bottom <= top) {
    return {};
  }
  return PixelRect{left, top, right - left, bottom - top}.Intersect(full);
}

bool CompositorOpenGL::SignalWhenRenderingComplete(
    const std::vector<PoolBuffer*>& buffers,
    const std::vector<Window::SyncPoints>& sync) {
  GpuDevice* gpu = egl_->gpu();
  const EGLint attributes[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID,
                               EGL_NO_NATIVE_FENCE_FD_ANDROID, EGL_NONE};
  EGLSyncKHR egl_sync = gpu->CreateSync(
      gpu->display(), EGL_SYNC_NATIVE_FENCE_ANDROID, attributes);
  // The fence is only created once the commands are submitted.
  glFlush();
  if (egl_sync != EGL_NO_SYNC_KHR) {
    // One fence covers the rendering into all the buffers.
    fml::UniqueFD fence(gpu->DupNativeFenceFD(gpu->display(), egl_sync));
    gpu->DestroySync(gpu->display(), egl_sync);
    bool imported = fence.is_valid();
    for (size_t i = 0; imported && i < buffers.size(); i++) {
      imported =
          buffers[i]->timeline->ImportSyncFile(sync[i].acquire_point, fence);
    }
    if (imported) {
      return true;
    }
  }

  FML_DLOG(WARNING) << "Unable to create fence, waiting for rendering";
  glFinish();
  for (size_t i = 0; i < buffers.size(); i++) {
    if (!buffers[i]->timeline->Signal(sync[i].acquire_point)) {
      return false;
    }
  }
  return true;
}

CompositorOpenGL::PoolBuffer* CompositorOpenGL::GetBuffer(FlutterViewId view_id,
                                                          int width,
                                                          int height) {
  DispatchBufferEvents();
  RemoveUnusedPools();

  auto& pool = pools_[view_id];
  TrimPool(pool, width, height);

  PoolBuffer* pool_buffer = FindFreeBuffer(pool, width, height);
  if (pool_buffer != nullptr) {
    WaitForRelease(pool_buffer);
    return pool_buffer;
  }

  // Each backing store in a frame needs its own set of buffers.
  size_t in_use = std::count_if(pool.begin(), pool.end(),
                                [](const auto& b) { return b->in_use; });
  size_t per_frame = std::max(backing_stores_per_frame_[view_id], in_use + 1);
  if (pool.size() >= kMaxPoolSize * per_frame) {
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
  std::unique_ptr<RenderBuffer> buffer = AllocateBuffer(width, height);
  if (!buffer) {
    return nullptr;
  }
  buffer_modifier_ = buffer->modifier();
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

std::unique_ptr<RenderBuffer> CompositorOpenGL::AllocateBuffer(int width,
                                                               int height) {
  if (display_ == nullptr) {
    return TextureBuffer::Create(egl_, width, height, depth_stencil_);
  }
  if (use_dmabuf_) {
    std::unique_ptr<RenderBuffer> buffer = DmabufBuffer::Create(
        egl_, dmabuf_wrapper_, display_->GetModifiers(DmabufBuffer::kFormat),
        width, height, depth_stencil_);
    if (buffer) {
      dmabuf_allocated_ = true;
      return buffer;
    }
    if (dmabuf_allocated_) {
      return nullptr;
    }
    WarnShmFallback("a buffer could not be allocated");
    use_dmabuf_ = false;
    explicit_sync_ = false;
  }
  return ShmBuffer::Create(egl_, shm_wrapper_, width, height, depth_stencil_);
}

void CompositorOpenGL::RemoveUnusedPools() {
  std::scoped_lock lock(views_mutex_);
  for (auto it = pools_.begin(); it != pools_.end();) {
    bool in_use = std::any_of(it->second.begin(), it->second.end(),
                              [](const auto& b) { return b->in_use; });
    if (views_.count(it->first) == 0 && !in_use) {
      backing_stores_per_frame_.erase(it->first);
      it = pools_.erase(it);
    } else {
      ++it;
    }
  }
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
  if (queue_ == nullptr) {
    return;
  }
  wl_display_dispatch_queue_pending(display_->display(), queue_);
}

bool CompositorOpenGL::WaitForBufferEvents(int timeout_ms) {
  if (queue_ == nullptr) {
    return false;
  }
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
