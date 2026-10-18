// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_COMPOSITOR_OPENGL_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_COMPOSITOR_OPENGL_H_

#include <wayland-client.h>

#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/dmabuf_buffer.h"
#include "flutter/shell/platform/linux/drm_timeline.h"
#include "flutter/shell/platform/linux/egl_manager.h"
#include "flutter/shell/platform/linux/wayland_display.h"

namespace flutter {

class FlutterLinuxView;

// Implements Flutter's compositor using OpenGL.
//
// Flutter renders each frame into a dmabuf, which is then handed directly to
// the Wayland compositor. No copies are made.
//
// If the compositor and GPU support it, access to buffers is synchronized
// explicitly with DRM timeline syncobjs (wp_linux_drm_syncobj_manager_v1).
// The compositor waits for a fence signalled when rendering completes, and
// Flutter waits on the GPU for a fence signalled when the compositor has
// finished reading a buffer before rendering into it again. Otherwise
// implicit synchronization and wl_buffer.release are used.
//
// Backing stores are created, collected and presented on the raster thread.
class CompositorOpenGL {
 public:
  // If |depth_stencil| is true, backing stores have a depth/stencil
  // attachment (required by Impeller).
  CompositorOpenGL(EGLManager* egl,
                   WaylandDisplay* display,
                   bool depth_stencil);
  ~CompositorOpenGL();

  // Registers |view| to have frames for |view_id| presented to it. Called on
  // the platform thread.
  void AddView(FlutterViewId view_id, FlutterLinuxView* view);

  // Stops presenting frames to the view with |view_id|. Called on the
  // platform thread.
  void RemoveView(FlutterViewId view_id);

  // |FlutterCompositor.create_backing_store_callback|
  bool CreateBackingStore(const FlutterBackingStoreConfig& config,
                          FlutterBackingStore* result);

  // |FlutterCompositor.collect_backing_store_callback|
  bool CollectBackingStore(const FlutterBackingStore* store);

  // |FlutterCompositor.present_view_callback|
  bool Present(FlutterViewId view_id,
               const FlutterLayer** layers,
               size_t layers_count);

 private:
  // A buffer that can be used as a backing store.
  struct PoolBuffer {
    std::unique_ptr<DmabufBuffer> buffer;

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

  // Removes free buffers from the pool that aren't the given size.
  void TrimPool(std::vector<std::unique_ptr<PoolBuffer>>& pool,
                int width,
                int height);

  // Returns a free buffer of the given size from |pool| or nullptr.
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

  // Flushes rendering and sets |point| on |timeline| to signal when it
  // completes.
  bool SignalWhenRenderingComplete(DrmTimeline* timeline, uint64_t point);

  // Processes buffer release events from the compositor.
  void DispatchBufferEvents();

  // Waits for a buffer release event, up to |timeout_ms|. Returns false if
  // the wait timed out or failed.
  bool WaitForBufferEvents(int timeout_ms);

  EGLManager* egl_;
  WaylandDisplay* display_;
  bool depth_stencil_;

  // True if buffers are explicitly synchronized with the compositor.
  bool explicit_sync_ = false;

  // Events for buffers are on a separate queue so they can be processed on
  // the raster thread independently of the platform thread.
  wl_event_queue* queue_ = nullptr;
  zwp_linux_dmabuf_v1* dmabuf_wrapper_ = nullptr;

  // Buffers for each view. Only accessed on the raster thread.
  std::map<FlutterViewId, std::vector<std::unique_ptr<PoolBuffer>>> pools_;

  std::mutex views_mutex_;
  std::map<FlutterViewId, FlutterLinuxView*> views_;

  bool warned_multiple_layers_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(CompositorOpenGL);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_COMPOSITOR_OPENGL_H_
