// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_NEXT_FLUTTER_LINUX_VIEW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_NEXT_FLUTTER_LINUX_VIEW_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux_next/window.h"

namespace flutter {

class FlutterLinuxEngine;

// A window that shows the output of an engine for one of its views.
//
// Must be used on the platform thread, except where noted.
class FlutterLinuxView : public Window::Delegate {
 public:
  // Creates a view of |engine| with |view_id|, in a window created with
  // |params|. |engine| must outlive the view. Returns nullptr on failure.
  //
  // Views are normally created with FlutterLinuxEngine::CreateImplicitView
  // or FlutterLinuxEngine::CreateView, which register them with the engine.
  static std::unique_ptr<FlutterLinuxView> Create(FlutterLinuxEngine* engine,
                                                  FlutterViewId view_id,
                                                  const Window::Params& params);

  ~FlutterLinuxView() override;

  FlutterViewId view_id() const { return view_id_; }

  FlutterLinuxEngine* engine() const { return engine_; }

  Window* window() const { return window_.get(); }

  // The current size and scale of the view.
  FlutterWindowMetricsEvent GetWindowMetrics() const;

  // Tells the engine the current size and scale of the view.
  void SendWindowMetrics();

  // Shows a frame rendered by the engine. Called on the raster thread.
  void PresentBuffer(const uint8_t* pixels,
                     int width,
                     int height,
                     size_t row_bytes);

  // |Window::Delegate|
  void OnWindowConfigure() override;

  // |Window::Delegate|
  void OnWindowClose() override;

 private:
  FlutterLinuxView(FlutterLinuxEngine* engine, FlutterViewId view_id);

  // Shows the last frame presented, on the platform thread.
  void ShowFrame();

  FlutterLinuxEngine* engine_;
  FlutterViewId view_id_;
  std::unique_ptr<Window> window_;

  // The last frame presented, waiting to be shown on the platform thread.
  std::mutex frame_mutex_;
  std::vector<uint8_t> frame_;
  int frame_width_ = 0;
  int frame_height_ = 0;
  size_t frame_row_bytes_ = 0;

  // Tasks posted to the platform thread hold a weak reference to this, so
  // they don't run after the view is destroyed.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterLinuxView);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_NEXT_FLUTTER_LINUX_VIEW_H_
