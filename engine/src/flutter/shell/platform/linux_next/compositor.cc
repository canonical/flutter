// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_next/compositor.h"

#include "flutter/fml/logging.h"

namespace flutter {

Compositor::Compositor() = default;

Compositor::~Compositor() = default;

void Compositor::GetFlutterCompositor(FlutterCompositor* compositor) {
  *compositor = {};
  compositor->struct_size = sizeof(FlutterCompositor);
  compositor->user_data = this;
  compositor->create_backing_store_callback =
      [](const FlutterBackingStoreConfig* config,
         FlutterBackingStore* backing_store_out, void* user_data) {
        return static_cast<Compositor*>(user_data)->CreateBackingStore(
            *config, backing_store_out);
      };
  compositor->collect_backing_store_callback =
      [](const FlutterBackingStore* backing_store, void* user_data) {
        return static_cast<Compositor*>(user_data)->CollectBackingStore(
            backing_store);
      };
  compositor->present_view_callback = [](const FlutterPresentViewInfo* info) {
    return static_cast<Compositor*>(info->user_data)
        ->Present(info->view_id, info->layers, info->layers_count);
  };
}

void Compositor::AddView(FlutterViewId view_id, FlutterLinuxView* view) {
  std::scoped_lock lock(views_mutex_);
  views_[view_id] = view;
}

void Compositor::RemoveView(FlutterViewId view_id) {
  // Holding the lock waits for any frame being presented to the view.
  std::scoped_lock lock(views_mutex_);
  views_.erase(view_id);
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

  std::scoped_lock lock(views_mutex_);
  auto it = views_.find(view_id);
  if (it == views_.end()) {
    // The view has been removed.
    return true;
  }
  return PresentToView(it->second, layers, layers_count);
}

}  // namespace flutter
