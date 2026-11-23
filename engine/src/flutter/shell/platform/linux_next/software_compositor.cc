// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_next/software_compositor.h"

#include <cmath>
#include <cstdlib>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux_next/flutter_linux_view.h"

namespace flutter {

namespace {

constexpr size_t kBytesPerPixel = 4;

}  // namespace

SoftwareCompositor::SoftwareCompositor() = default;

SoftwareCompositor::~SoftwareCompositor() = default;

FlutterRendererConfig SoftwareCompositor::GetRendererConfig() {
  FlutterRendererConfig config = {};
  config.type = kSoftware;
  config.software.struct_size = sizeof(FlutterSoftwareRendererConfig);
  // Frames are presented by the compositor, but the engine requires this.
  config.software.surface_present_callback =
      [](void* user_data, const void* allocation, size_t row_bytes,
         size_t height) { return false; };
  return config;
}

bool SoftwareCompositor::CreateBackingStore(
    const FlutterBackingStoreConfig& config,
    FlutterBackingStore* result) {
  size_t width = std::ceil(config.size.width);
  size_t height = std::ceil(config.size.height);
  size_t row_bytes = width * kBytesPerPixel;
  void* allocation = calloc(height, row_bytes);
  if (allocation == nullptr) {
    FML_LOG(ERROR) << "Failed to allocate " << width << "x" << height
                   << " backing store";
    return false;
  }

  result->type = kFlutterBackingStoreTypeSoftware;
  result->software.allocation = allocation;
  result->software.row_bytes = row_bytes;
  result->software.height = height;
  // The engine calls this once it has stopped rendering into the allocation,
  // which may be after the backing store is collected.
  result->software.user_data = allocation;
  result->software.destruction_callback = free;
  return true;
}

bool SoftwareCompositor::CollectBackingStore(const FlutterBackingStore* store) {
  // The allocation is freed by its destruction callback.
  return true;
}

bool SoftwareCompositor::PresentToView(FlutterLinuxView* view,
                                       const FlutterLayer** layers,
                                       size_t layers_count) {
  // Without platform views, frames are a single backing store layer.
  const FlutterBackingStore* store = layers[0]->backing_store;
  if (store->type != kFlutterBackingStoreTypeSoftware) {
    FML_LOG(ERROR) << "Backing store is not a software backing store";
    return false;
  }

  const FlutterSoftwareBackingStore& software = store->software;
  view->PresentBuffer(static_cast<const uint8_t*>(software.allocation),
                      software.row_bytes / kBytesPerPixel, software.height,
                      software.row_bytes);
  return true;
}

}  // namespace flutter
