// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_VIEW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_VIEW_H_

#include <functional>
#include <memory>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/pointer_event_converter.h"
#include "flutter/shell/platform/linux/render_buffer.h"
#include "flutter/shell/platform/linux/wayland_window.h"

namespace flutter {

class FlutterLinuxEngine;

// A Flutter view shown in a top-level window.
class FlutterLinuxView : public WaylandWindow::Delegate,
                         public PointerEventConverter::Delegate {
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

  // Sets a function to call when the user requests the window be closed,
  // replacing the default behavior of quitting the application. The view may
  // be destroyed from |callback|.
  void SetCloseCallback(std::function<void()> callback) {
    close_callback_ = std::move(callback);
  }

  // Returns the current window metrics for this view.
  FlutterWindowMetricsEvent GetWindowMetrics() const;

  // Sends the current window metrics to the engine.
  void SendWindowMetrics();

  // Shows a rendered frame, synchronized with |sync| if provided. Called on
  // the raster thread.
  void PresentBuffer(RenderBuffer* buffer,
                     const WaylandWindow::SyncPoints* sync);

  // |WaylandWindow::Delegate|
  void OnWindowConfigure() override;

  // |WaylandWindow::Delegate|
  void OnWindowClose() override;

  // |WaylandWindow::Delegate|
  void OnWindowPresented(uint64_t presentation_time_ns,
                         uint64_t refresh_ns) override;

  // |WaylandWindow::Delegate|
  void OnWindowFrameDone() override;

  // |PointerEventConverter::Delegate|
  double GetPointerScale() override;
  void SendPointerEvents(const FlutterPointerEvent* events,
                         size_t count) override;
  void OnPointerEntered() override;

  // The number of presented frames the compositor is not yet ready to
  // follow with another frame.
  size_t frames_in_flight() const { return window_->frames_in_flight(); }

 private:
  FlutterLinuxView(FlutterLinuxEngine* engine, FlutterViewId view_id);

  FlutterLinuxEngine* engine_;
  FlutterViewId view_id_;
  std::unique_ptr<WaylandWindow> window_;
  std::function<void()> close_callback_;

  // Converts input in the window to Flutter events.
  std::unique_ptr<PointerEventConverter> pointer_converter_;

  // Tasks posted by the view hold a weak reference to this, so they don't
  // run after the view is destroyed.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterLinuxView);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_VIEW_H_
