// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_COMPOSITOR_OPENGL_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_COMPOSITOR_OPENGL_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/corner_mask.h"
#include "flutter/shell/platform/linux/display.h"
#include "flutter/shell/platform/linux/egl_manager.h"
#include "flutter/shell/platform/linux/platform_view.h"
#include "flutter/shell/platform/linux/platform_view_geometry.h"
#include "flutter/shell/platform/linux/render_buffer.h"
#include "flutter/shell/platform/linux/wayland/drm_timeline.h"
#include "flutter/shell/platform/linux/wayland/protocols/wayland-client-protocol.h"
#include "flutter/shell/platform/linux/wayland/wayland_display.h"
#include "flutter/shell/platform/linux/window.h"

namespace flutter {

class FlutterLinuxView;

// Implements Flutter's compositor using OpenGL.
//
// Flutter renders each frame into a dmabuf, which is then handed directly to
// the Wayland compositor. No copies are made.
//
// If the compositor or GPU can't share dmabufs, each frame is instead copied
// from the GPU into shared memory (wl_shm).
//
// If the compositor and GPU support it, access to buffers is synchronized
// explicitly with DRM timeline syncobjs (wp_linux_drm_syncobj_manager_v1).
// The compositor waits for a fence signalled when rendering completes, and
// Flutter waits on the GPU for a fence signalled when the compositor has
// finished reading a buffer before rendering into it again. Otherwise
// implicit synchronization and wl_buffer.release are used.
//
// On X11, Flutter renders into GL textures that the window copies into its
// own buffers when presenting.
//
// Backing stores are created, collected and presented on the raster thread.
class CompositorOpenGL {
 public:
  // If |depth_stencil| is true, backing stores have a depth/stencil
  // attachment (required by Impeller).
  CompositorOpenGL(EGLManager* egl, Display* display, bool depth_stencil);
  ~CompositorOpenGL();

  // Registers |view| to have frames for |view_id| presented to it. Called on
  // the platform thread.
  void AddView(FlutterViewId view_id, FlutterLinuxView* view);

  // Stops presenting frames to the view with |view_id|. Called on the
  // platform thread.
  void RemoveView(FlutterViewId view_id);

  // True if buffers are shared with the compositor as dmabufs, false if
  // frames are copied into shared memory. May be called from any thread.
  bool uses_dmabuf() const { return use_dmabuf_; }

  // True if buffers are explicitly synchronized with the compositor. May be
  // called from any thread.
  bool explicit_sync() const { return explicit_sync_; }

  // The DRM format modifier of the most recently allocated buffer, or
  // DRM_FORMAT_MOD_INVALID if its layout was chosen implicitly or no buffer
  // has been allocated yet. Shared memory buffers are DRM_FORMAT_MOD_LINEAR.
  // May be called from any thread.
  uint64_t buffer_modifier() const { return buffer_modifier_; }

  // |FlutterCompositor.create_backing_store_callback|
  bool CreateBackingStore(const FlutterBackingStoreConfig& config,
                          FlutterBackingStore* result);

  // |FlutterCompositor.collect_backing_store_callback|
  bool CollectBackingStore(const FlutterBackingStore* store);

  // |FlutterCompositor.present_view_callback|
  bool Present(FlutterViewId view_id,
               const FlutterLayer** layers,
               size_t layers_count);

  // Returns the platform view with an ID, or nullptr. Called on the raster
  // thread.
  using PlatformViewLookup =
      std::function<std::shared_ptr<LinuxPlatformView>(int64_t id)>;

  // Sets how platform views in frames are found. Must be called before the
  // engine runs.
  void SetPlatformViewLookup(PlatformViewLookup lookup);

 private:
  // A buffer that can be used as a backing store.
  struct PoolBuffer {
    std::unique_ptr<RenderBuffer> buffer;

    // True while Flutter is using the buffer as a backing store.
    bool in_use = false;

    // The timeline used to explicitly synchronize this buffer, or nullptr if
    // implicit synchronization is used. Each buffer has its own timeline as
    // the compositor may release buffers out of order.
    std::unique_ptr<DrmTimeline> timeline;

    // The last point used on |timeline|.
    uint64_t last_point = 0;

    // The point the compositor signals when it has finished with the buffer,
    // or zero if there is nothing to wait for.
    uint64_t release_point = 0;
  };

  // Returns a buffer of the given size from the pool for |view_id|,
  // allocating one if necessary.
  PoolBuffer* GetBuffer(FlutterViewId view_id, int width, int height);

  // Allocates a new buffer of the given size. Falls back to shared memory if
  // the first dmabuf can't be allocated.
  std::unique_ptr<RenderBuffer> AllocateBuffer(int width, int height);

  // Removes free buffers from the pool that aren't the given size.
  void TrimPool(std::vector<std::unique_ptr<PoolBuffer>>& pool,
                int width,
                int height);

  // Returns a free buffer of the given size from |pool| or nullptr.
  // Frees the buffers of views that have been removed, once the engine has
  // finished with them.
  void RemoveUnusedPools();

  PoolBuffer* FindFreeBuffer(std::vector<std::unique_ptr<PoolBuffer>>& pool,
                             int width,
                             int height);

  // True if the compositor has finished with |pool_buffer|. With explicit
  // synchronization the compositor may still be reading from it on the GPU,
  // which WaitForRelease() waits for.
  bool IsReleased(const PoolBuffer* pool_buffer) const;

  // True if |pool_buffer| can be used as a backing store.
  bool IsFree(const PoolBuffer* pool_buffer) const;

  // Ensures rendering into |pool_buffer| waits until the compositor has
  // finished reading from it.
  void WaitForRelease(PoolBuffer* pool_buffer);

  // Waits until the compositor releases a buffer in |pool|, up to
  // |timeout_ms|. Returns false if the wait timed out or failed.
  bool WaitForReleasedBuffer(std::vector<std::unique_ptr<PoolBuffer>>& pool,
                             int timeout_ms);

  // Shows |layers| in |view|. The first layer is a backing store.
  bool PresentToView(FlutterLinuxView* view,
                     const FlutterLayer** layers,
                     size_t layers_count);

  // The bounds of the content drawn in a backing store |layer| of
  // |width| x |height| pixels. Empty if nothing was drawn.
  static PixelRect GetPaintBounds(const FlutterLayer* layer,
                                  int width,
                                  int height);

  // Flushes rendering and sets the acquire point in |sync| on the timeline
  // of each of |buffers| to signal when it completes.
  bool SignalWhenRenderingComplete(const std::vector<PoolBuffer*>& buffers,
                                   const std::vector<Window::SyncPoints>& sync);

  // Processes buffer release events from the compositor.
  void DispatchBufferEvents();

  // Waits for a buffer release event, up to |timeout_ms|. Returns false if
  // the wait timed out or failed.
  bool WaitForBufferEvents(int timeout_ms);

  EGLManager* egl_;
  // The display if it is a Wayland display, otherwise nullptr.
  WaylandDisplay* display_;
  bool depth_stencil_;

  // True if buffers are shared as dmabufs.
  std::atomic<bool> use_dmabuf_;

  // True once a dmabuf has been allocated, after which there is no fallback
  // to shared memory.
  bool dmabuf_allocated_ = false;

  // True if buffers are explicitly synchronized with the compositor.
  std::atomic<bool> explicit_sync_;

  std::atomic<uint64_t> buffer_modifier_;

  // Events for buffers are on a separate queue so they can be processed on
  // the raster thread independently of the platform thread.
  wl_event_queue* queue_ = nullptr;
  zwp_linux_dmabuf_v1* dmabuf_wrapper_ = nullptr;
  wl_shm* shm_wrapper_ = nullptr;

  // Buffers for each view. Only accessed on the raster thread.
  std::map<FlutterViewId, std::vector<std::unique_ptr<PoolBuffer>>> pools_;

  std::mutex views_mutex_;
  std::map<FlutterViewId, FlutterLinuxView*> views_;

  // The view being presented to. The lock isn't held while presenting, as
  // that may wait for the platform thread, so views are only removed once
  // they are not being presented to. Guarded by |views_mutex_|.
  std::optional<FlutterViewId> presenting_view_;
  std::condition_variable presented_condition_;

  // The number of backing stores in the last frame of each view. Only
  // accessed on the raster thread.
  std::map<FlutterViewId, size_t> backing_stores_per_frame_;

  PlatformViewLookup platform_view_lookup_;

  // Rounds the corners of decorated windows. Only used on the raster thread.
  std::unique_ptr<CornerMask> corner_mask_;

  FML_DISALLOW_COPY_AND_ASSIGN(CompositorOpenGL);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_COMPOSITOR_OPENGL_H_
