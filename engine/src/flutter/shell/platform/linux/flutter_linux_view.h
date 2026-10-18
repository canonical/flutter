// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_VIEW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_VIEW_H_

#include <memory>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/dmabuf_buffer.h"
#include "flutter/shell/platform/linux/wayland_window.h"

namespace flutter {

class FlutterLinuxEngine;

// A Flutter view shown in a top-level window.
class FlutterLinuxView : public WaylandWindow::Delegate {
 public:
  // Creates a view in a new window of the given logical size. Returns nullptr
  // on failure.
  static std::unique_ptr<FlutterLinuxView> Create(FlutterLinuxEngine* engine,
                                                  FlutterViewId view_id,
                                                  int width,
                                                  int height);

  ~FlutterLinuxView() override;

  FlutterViewId view_id() const { return view_id_; }
  FlutterLinuxEngine* engine() const { return engine_; }
  WaylandWindow* window() const { return window_.get(); }

  // Returns the current window metrics for this view.
  FlutterWindowMetricsEvent GetWindowMetrics() const;

  // Sends the current window metrics to the engine.
  void SendWindowMetrics();

  // Shows a rendered frame. Called on the raster thread.
  void PresentBuffer(DmabufBuffer* buffer);

  // |WaylandWindow::Delegate|
  void OnWindowConfigure() override;

  // |WaylandWindow::Delegate|
  void OnWindowClose() override;

 private:
  FlutterLinuxView(FlutterLinuxEngine* engine, FlutterViewId view_id);

  FlutterLinuxEngine* engine_;
  FlutterViewId view_id_;
  std::unique_ptr<WaylandWindow> window_;

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterLinuxView);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_VIEW_H_
