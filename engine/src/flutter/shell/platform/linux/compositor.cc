// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/compositor.h"

#include <drm_fourcc.h>
#include <linux/dma-buf.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

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

// Waits on the CPU for |fence| to signal, up to |timeout_ms|.
void WaitForFence(int fence, int timeout_ms) {
  if (fence < 0) {
    return;
  }
  pollfd fd = {.fd = fence, .events = POLLIN};
  if (FML_HANDLE_EINTR(poll(&fd, 1, timeout_ms)) <= 0) {
    FML_DLOG(WARNING) << "Timed out waiting for fence";
  }
}

// Adds |fence| to the fences implicitly synchronizing access to |dmabuf|, so
// readers wait for it. Returns false if the kernel doesn't support this.
bool ImportDmabufWriteFence(int dmabuf, int fence) {
  dma_buf_import_sync_file import = {
      .flags = DMA_BUF_SYNC_WRITE,
      .fd = fence,
  };
  return FML_HANDLE_EINTR(
             ioctl(dmabuf, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &import)) == 0;
}

// Returns a sync file that signals when all access to |dmabuf| that is
// implicitly synchronized completes, or an invalid fd on failure.
fml::UniqueFD ExportDmabufFence(int dmabuf) {
  dma_buf_export_sync_file exported = {
      .flags = DMA_BUF_SYNC_WRITE,
      .fd = -1,
  };
  if (FML_HANDLE_EINTR(
          ioctl(dmabuf, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exported)) != 0) {
    return fml::UniqueFD();
  }
  return fml::UniqueFD(exported.fd);
}

// Makes the render context current for the lifetime of this object if it
// isn't already. The engine makes it current while rendering with OpenGL, but
// not when rendering with Vulkan.
class ScopedRenderContext {
 public:
  explicit ScopedRenderContext(EGLManager* egl)
      : egl_(egl),
        made_current_(!egl->IsRenderContextCurrent() &&
                      egl->MakeRenderContextCurrent()) {}
  ~ScopedRenderContext() {
    if (made_current_) {
      egl_->ClearCurrent();
    }
  }

  bool is_current() const { return egl_->IsRenderContextCurrent(); }

 private:
  EGLManager* egl_;
  bool made_current_;
};

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

Compositor::Compositor(EGLManager* egl,
                       Display* display,
                       bool depth_stencil,
                       VulkanDevice* vulkan)
    : egl_(egl),
      vulkan_(vulkan),
      display_(display->backend() == Display::Backend::kWayland
                   ? static_cast<WaylandDisplay*>(display)
                   : nullptr),
      depth_stencil_(depth_stencil),
      use_dmabuf_(false),
      explicit_sync_(false),
      buffer_modifier_(DRM_FORMAT_MOD_INVALID) {
  const char* unsupported_reason = nullptr;
  if (display_ != nullptr) {
    queue_ = wl_display_create_queue(display_->display());
    shm_wrapper_ =
        static_cast<wl_shm*>(wl_proxy_create_wrapper(display_->shm()));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(shm_wrapper_), queue_);

    if (display_->dmabuf() == nullptr) {
      unsupported_reason = "the Wayland compositor does not support it";
    } else if (!egl_->gpu()->supports_dmabuf_import()) {
      unsupported_reason = "EGL does not support EGL_EXT_image_dma_buf_import";
    } else if (display_->GetModifiers(DmabufBuffer::kFormat).empty()) {
      unsupported_reason = "the Wayland compositor does not support ARGB8888";
    }
    if (unsupported_reason != nullptr) {
      WarnShmFallback(unsupported_reason);
    }
  }

  // Vulkan renders into dmabufs that OpenGL presents, synchronized with sync
  // files.
  if (vulkan_ != nullptr && (unsupported_reason != nullptr ||
                             !egl_->gpu()->supports_dmabuf_modifiers() ||
                             !egl_->gpu()->supports_explicit_sync())) {
    FML_LOG(WARNING) << "Buffers can't be shared with Vulkan, using OpenGL";
    vulkan_ = nullptr;
  }

  if (display_ == nullptr || unsupported_reason != nullptr) {
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

void Compositor::DisableVulkan() {
  FML_DCHECK(pools_.empty());
  vulkan_ = nullptr;
}

Compositor::~Compositor() {
  if (vulkan_ != nullptr) {
    vulkan_->WaitIdle();
  }
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

void Compositor::AddView(FlutterViewId view_id, FlutterLinuxView* view) {
  std::scoped_lock lock(views_mutex_);
  views_[view_id] = view;
}

void Compositor::RemoveView(FlutterViewId view_id) {
  std::unique_lock<std::mutex> lock(views_mutex_);
  views_.erase(view_id);
  presented_condition_.wait(
      lock, [this, view_id]() { return presenting_view_ != view_id; });
}

bool Compositor::CreateBackingStore(const FlutterBackingStoreConfig& config,
                                    FlutterBackingStore* result) {
  int width = static_cast<int>(config.size.width);
  int height = static_cast<int>(config.size.height);
  if (width <= 0 || height <= 0) {
    return false;
  }

  ScopedRenderContext context(egl_);
  PoolBuffer* pool_buffer = GetBuffer(config.view_id, width, height);
  if (pool_buffer == nullptr) {
    return false;
  }
  pool_buffer->in_use = true;

  result->user_data = pool_buffer;
  if (vulkan_ != nullptr) {
    pool_buffer->vulkan_image = {
        .struct_size = sizeof(FlutterVulkanImage),
        .image = reinterpret_cast<FlutterVulkanImageHandle>(
            pool_buffer->buffer->vulkan_image()),
        .format = VulkanDevice::kFormat,
    };
    result->type = kFlutterBackingStoreTypeVulkan;
    result->vulkan.struct_size = sizeof(FlutterVulkanBackingStore);
    result->vulkan.image = &pool_buffer->vulkan_image;
    result->vulkan.user_data = nullptr;
    result->vulkan.destruction_callback = [](void* user_data) {};
    return true;
  }
  result->type = kFlutterBackingStoreTypeOpenGL;
  result->open_gl.type = kFlutterOpenGLTargetTypeFramebuffer;
  result->open_gl.framebuffer.target = RenderBuffer::kGLFormat;
  result->open_gl.framebuffer.name = pool_buffer->buffer->framebuffer();
  result->open_gl.framebuffer.user_data = nullptr;
  result->open_gl.framebuffer.destruction_callback = [](void* user_data) {};
  return true;
}

bool Compositor::CollectBackingStore(const FlutterBackingStore* store) {
  // The buffer stays in the pool for reuse once the compositor releases it.
  static_cast<PoolBuffer*>(store->user_data)->in_use = false;
  return true;
}

bool Compositor::Present(FlutterViewId view_id,
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
  bool result = false;
  {
    ScopedRenderContext context(egl_);
    result = context.is_current() && PresentToView(view, layers, layers_count);
  }
  {
    std::scoped_lock lock(views_mutex_);
    presenting_view_.reset();
  }
  presented_condition_.notify_all();
  return result;
}

void Compositor::SetPlatformViewLookup(PlatformViewLookup lookup) {
  platform_view_lookup_ = std::move(lookup);
}

bool Compositor::PresentToView(FlutterLinuxView* view,
                               const FlutterLayer** layers,
                               size_t layers_count) {
  auto* root = static_cast<PoolBuffer*>(layers[0]->backing_store->user_data);
  RenderBuffer* root_buffer = root->buffer.get();
  int width = root_buffer->width();
  int height = root_buffer->height();

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

  if (vulkan_ != nullptr) {
    WaitForVulkan(buffers);
  }

  double corner_radius = view->window()->ContentCornerRadius(width, height);
  if (corner_radius > 0) {
    if (!corner_mask_) {
      corner_mask_ = std::make_unique<CornerMask>();
    }
    corner_mask_->Apply(root_buffer->framebuffer(), width, height,
                        corner_radius, root_buffer->top_row_first());
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
    if (vulkan_ != nullptr) {
      SetRenderFences(buffers);
    }
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
  if (vulkan_ != nullptr) {
    SetRenderFences(buffers);
  }
  return true;
}

void Compositor::WaitForVulkan(const std::vector<PoolBuffer*>& buffers) {
  std::vector<VkImage> images;
  for (PoolBuffer* pool_buffer : buffers) {
    images.push_back(pool_buffer->buffer->vulkan_image());
  }
  fml::UniqueFD fence = vulkan_->ReleaseImages(images);
  if (!fence.is_valid()) {
    // Rendering has completed.
    return;
  }

  // Vulkan doesn't add its fences to the dmabufs, so they are added for
  // compositors that synchronize implicitly.
  if (display_ != nullptr && buffers[0]->timeline == nullptr) {
    for (PoolBuffer* pool_buffer : buffers) {
      if (!ImportDmabufWriteFence(pool_buffer->buffer->dmabuf_fd(),
                                  fence.get())) {
        WaitForFence(fence.get(), kBufferWaitTimeoutMs);
        return;
      }
    }
  }

  if (!GpuWait(fence.get())) {
    WaitForFence(fence.get(), kBufferWaitTimeoutMs);
  }
}

bool Compositor::GpuWait(int fence) {
  GpuDevice* gpu = egl_->gpu();
  if (gpu->WaitSync == nullptr || fence < 0 ||
      !egl_->IsRenderContextCurrent()) {
    return false;
  }
  fml::UniqueFD fd(dup(fence));
  const EGLint attributes[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd.get(),
                               EGL_NONE};
  EGLSyncKHR sync = gpu->CreateSync(gpu->display(),
                                    EGL_SYNC_NATIVE_FENCE_ANDROID, attributes);
  if (sync == EGL_NO_SYNC_KHR) {
    return false;
  }
  // EGL owns the fd now.
  [[maybe_unused]] int released = fd.release();
  bool waited = gpu->WaitSync(gpu->display(), sync, 0) == EGL_TRUE;
  gpu->DestroySync(gpu->display(), sync);
  return waited;
}

void Compositor::SetRenderFences(const std::vector<PoolBuffer*>& buffers) {
  GpuDevice* gpu = egl_->gpu();
  const EGLint attributes[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID,
                               EGL_NO_NATIVE_FENCE_FD_ANDROID, EGL_NONE};
  EGLSyncKHR sync = gpu->CreateSync(gpu->display(),
                                    EGL_SYNC_NATIVE_FENCE_ANDROID, attributes);
  glFlush();
  fml::UniqueFD fence;
  if (sync != EGL_NO_SYNC_KHR) {
    fence.reset(gpu->DupNativeFenceFD(gpu->display(), sync));
    gpu->DestroySync(gpu->display(), sync);
  }
  if (!fence.is_valid()) {
    glFinish();
  }
  for (PoolBuffer* pool_buffer : buffers) {
    pool_buffer->render_fence.reset(fence.is_valid() ? dup(fence.get()) : -1);
  }
}

void Compositor::WaitForRenderFence(PoolBuffer* pool_buffer) {
  WaitForFence(pool_buffer->render_fence.get(),
               kReleaseFenceTimeoutNs / 1000000);
  pool_buffer->render_fence.reset();
}

PixelRect Compositor::GetPaintBounds(const FlutterLayer* layer,
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

bool Compositor::SignalWhenRenderingComplete(
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

Compositor::PoolBuffer* Compositor::GetBuffer(FlutterViewId view_id,
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

std::unique_ptr<RenderBuffer> Compositor::AllocateBuffer(int width,
                                                         int height) {
  // Impeller's Vulkan backend allocates its own depth and stencil buffers.
  bool depth_stencil = depth_stencil_ && vulkan_ == nullptr;
  if (display_ == nullptr) {
    return TextureBuffer::Create(egl_, width, height, depth_stencil, vulkan_);
  }
  if (use_dmabuf_) {
    std::unique_ptr<RenderBuffer> buffer =
        DmabufBuffer::Create(egl_, vulkan_, dmabuf_wrapper_,
                             display_->GetModifiers(DmabufBuffer::kFormat),
                             width, height, depth_stencil);
    if (buffer) {
      dmabuf_allocated_ = true;
      return buffer;
    }
    // Vulkan can only render into dmabufs.
    if (dmabuf_allocated_ || vulkan_ != nullptr) {
      return nullptr;
    }
    WarnShmFallback("a buffer could not be allocated");
    use_dmabuf_ = false;
    explicit_sync_ = false;
  }
  return ShmBuffer::Create(egl_, shm_wrapper_, width, height, depth_stencil);
}

void Compositor::RemoveUnusedPools() {
  std::scoped_lock lock(views_mutex_);
  for (auto it = pools_.begin(); it != pools_.end();) {
    bool in_use = std::any_of(it->second.begin(), it->second.end(),
                              [](const auto& b) { return b->in_use; });
    if (views_.count(it->first) == 0 && !in_use) {
      for (auto& b : it->second) {
        WaitForRenderFence(b.get());
      }
      backing_stores_per_frame_.erase(it->first);
      it = pools_.erase(it);
    } else {
      ++it;
    }
  }
}

void Compositor::TrimPool(std::vector<std::unique_ptr<PoolBuffer>>& pool,
                          int width,
                          int height) {
  pool.erase(std::remove_if(pool.begin(), pool.end(),
                            [this, width, height](const auto& b) {
                              if (!IsFree(b.get()) ||
                                  (b->buffer->width() == width &&
                                   b->buffer->height() == height)) {
                                return false;
                              }
                              WaitForRenderFence(b.get());
                              return true;
                            }),
             pool.end());
}

Compositor::PoolBuffer* Compositor::FindFreeBuffer(
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

bool Compositor::IsReleased(const PoolBuffer* pool_buffer) const {
  if (pool_buffer->timeline == nullptr) {
    return !pool_buffer->buffer->busy();
  }
  return pool_buffer->release_point == 0 ||
         pool_buffer->timeline->IsAvailable(pool_buffer->release_point);
}

bool Compositor::IsFree(const PoolBuffer* pool_buffer) const {
  return !pool_buffer->in_use && IsReleased(pool_buffer);
}

void Compositor::WaitForRelease(PoolBuffer* pool_buffer) {
  if (vulkan_ != nullptr) {
    // Vulkan can't be made to wait for OpenGL or the compositor, so wait on
    // the CPU. The waits are usually complete as there are several buffers.
    WaitForRenderFence(pool_buffer);
    if (pool_buffer->timeline == nullptr && display_ != nullptr) {
      fml::UniqueFD fence = ExportDmabufFence(pool_buffer->buffer->dmabuf_fd());
      WaitForFence(fence.get(), kReleaseFenceTimeoutNs / 1000000);
      return;
    }
  }

  if (pool_buffer->timeline == nullptr || pool_buffer->release_point == 0) {
    return;
  }
  DrmTimeline* timeline = pool_buffer->timeline.get();
  uint64_t point = pool_buffer->release_point;
  pool_buffer->release_point = 0;

  // Have the GPU wait for the compositor's reads to complete before
  // rendering, so the CPU doesn't block.
  // Vulkan can't wait on OpenGL, so it waits on the CPU.
  if (vulkan_ == nullptr && GpuWait(timeline->ExportSyncFile(point).get())) {
    return;
  }

  if (!timeline->Wait(point, NowNanoseconds() + kReleaseFenceTimeoutNs)) {
    FML_DLOG(WARNING) << "Timed out waiting for compositor to finish with "
                         "buffer";
  }
}

bool Compositor::WaitForReleasedBuffer(
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

void Compositor::DispatchBufferEvents() {
  if (queue_ == nullptr) {
    return;
  }
  wl_display_dispatch_queue_pending(display_->display(), queue_);
}

bool Compositor::WaitForBufferEvents(int timeout_ms) {
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
