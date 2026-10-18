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
#include "flutter/shell/platform/linux/egl_manager.h"
#include "flutter/shell/platform/linux/wayland_display.h"

namespace flutter {

class FlutterLinuxView;

// Implements Flutter's compositor using OpenGL.
//
// Flutter renders each frame into a dmabuf, which is then handed directly to
// the Wayland compositor. No copies are made.
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

  // Processes buffer release events from the compositor.
  void DispatchBufferEvents();

  // Waits for a buffer release event, up to |timeout_ms|. Returns false if
  // the wait timed out or failed.
  bool WaitForBufferEvents(int timeout_ms);

  EGLManager* egl_;
  WaylandDisplay* display_;
  bool depth_stencil_;

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
