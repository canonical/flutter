// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_NEXT_COMPOSITOR_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_NEXT_COMPOSITOR_H_

#include <map>
#include <mutex>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"

namespace flutter {

class FlutterLinuxView;

// Provides the buffers the engine renders into, and presents the rendered
// frames in the view they were rendered for.
//
// Subclasses implement a way of rendering, which determines the type of
// backing stores used. Backing stores are created, collected and presented on
// the raster thread.
class Compositor {
 public:
  virtual ~Compositor();

  // The renderer configuration for the engine to render into this
  // compositor's backing stores.
  virtual FlutterRendererConfig GetRendererConfig() = 0;

  // Fills in |compositor| to have the engine use this object.
  void GetFlutterCompositor(FlutterCompositor* compositor);

  // Registers |view| to have frames for |view_id| presented to it. Called on
  // the platform thread.
  void AddView(FlutterViewId view_id, FlutterLinuxView* view);

  // Stops presenting frames to the view with |view_id|. Once this returns
  // the view is no longer used. Called on the platform thread.
  void RemoveView(FlutterViewId view_id);

  // |FlutterCompositor.create_backing_store_callback|
  virtual bool CreateBackingStore(const FlutterBackingStoreConfig& config,
                                  FlutterBackingStore* result) = 0;

  // |FlutterCompositor.collect_backing_store_callback|
  virtual bool CollectBackingStore(const FlutterBackingStore* store) = 0;

  // |FlutterCompositor.present_view_callback|
  bool Present(FlutterViewId view_id,
               const FlutterLayer** layers,
               size_t layers_count);

 protected:
  Compositor();

  // Presents a frame in |view|. The bottom layer is always a backing store.
  virtual bool PresentToView(FlutterLinuxView* view,
                             const FlutterLayer** layers,
                             size_t layers_count) = 0;

 private:
  // Protects |views_|, which is used on both the platform and raster threads.
  std::mutex views_mutex_;
  std::map<FlutterViewId, FlutterLinuxView*> views_;

  FML_DISALLOW_COPY_AND_ASSIGN(Compositor);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_NEXT_COMPOSITOR_H_
