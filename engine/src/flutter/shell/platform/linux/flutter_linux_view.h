// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_VIEW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_VIEW_H_

#include <atomic>
#include <functional>
#include <limits>
#include <memory>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/accessible_view.h"
#include "flutter/shell/platform/linux/pointer_event_converter.h"
#include "flutter/shell/platform/linux/render_buffer.h"
#include "flutter/shell/platform/linux/wayland_window.h"

namespace flutter {

class FlutterLinuxEngine;

// A Flutter view shown in a window.
class FlutterLinuxView : public WaylandWindow::Delegate,
                         public PointerEventConverter::Delegate,
                         public AccessibleView::Host {
 public:
  // How the size of the view is chosen.
  enum class Sizing {
    // The view fills the window.
    kWindow,
    // The window is sized to fit the content the view draws.
    kContent,
    // The window is sized to fit the first frame the view draws, then the
    // view fills the window.
    kInitialContent,
  };

  // Limits on the size of content that sizes its window, in logical pixels.
  struct Constraints {
    double min_width = 0;
    double min_height = 0;
    double max_width = std::numeric_limits<double>::infinity();
    double max_height = std::numeric_limits<double>::infinity();
  };

  // Creates a view in a new window. Returns nullptr on failure.
  static std::unique_ptr<FlutterLinuxView> Create(
      FlutterLinuxEngine* engine,
      FlutterViewId view_id,
      const WaylandWindow::Params& params,
      Sizing sizing,
      const Constraints& constraints);

  ~FlutterLinuxView() override;

  FlutterViewId view_id() const { return view_id_; }
  FlutterLinuxEngine* engine() const { return engine_; }
  WaylandWindow* window() const { return window_.get(); }

  // The view's semantics tree, exposed to assistive technologies.
  AccessibleView* accessible() const { return accessible_.get(); }

  // Sets a function to call when the user requests the window be closed,
  // replacing the default behavior of quitting the application. The view may
  // be destroyed from |callback|.
  void SetCloseCallback(std::function<void()> callback) {
    close_callback_ = std::move(callback);
  }

  // Sets a function to call when the window size or state changes, or the
  // window moves.
  void SetStateCallback(std::function<void()> callback) {
    state_callback_ = std::move(callback);
  }

  // Changes the limits of content that sizes its window.
  void SetConstraints(const Constraints& constraints);

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
  void OnWindowStateChanged() override;

  // |WaylandWindow::Delegate|
  void OnWindowTitleChanged() override;

  // |WaylandWindow::Delegate|
  void OnWindowDisplayChanged() override;

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

  // |AccessibleView::Host|
  std::string GetAccessibleTitle() override;
  AtspiRole GetAccessibleRole() override;
  AtspiStateSet GetWindowStates() override;
  std::vector<AtspiRelation> GetWindowRelations() override;
  AtspiRect GetWindowExtents() override;
  double GetPixelRatio() override;
  void DispatchSemanticsAction(uint64_t node_id,
                               FlutterSemanticsAction action,
                               std::vector<uint8_t> data) override;

  // The number of presented frames the compositor is not yet ready to
  // follow with another frame.
  size_t frames_in_flight() const { return window_->frames_in_flight(); }

 private:
  FlutterLinuxView(FlutterLinuxEngine* engine,
                   FlutterViewId view_id,
                   Sizing sizing,
                   const Constraints& constraints);

  // Resizes the window to fit content drawn at |width| x |height| physical
  // pixels with |scale|. Called on the platform thread.
  void OnContentResized(int width, int height, double scale);

  FlutterLinuxEngine* engine_;
  FlutterViewId view_id_;
  std::unique_ptr<WaylandWindow> window_;
  std::function<void()> close_callback_;
  std::function<void()> state_callback_;

  // How the view is sized, read on the raster thread.
  std::atomic<Sizing> sizing_;
  Constraints constraints_;

  // The size of the last frame presented, in physical pixels. Only accessed
  // on the raster thread.
  int presented_width_ = 0;
  int presented_height_ = 0;

  // Converts input in the window to Flutter events.
  std::unique_ptr<PointerEventConverter> pointer_converter_;

  std::unique_ptr<AccessibleView> accessible_;

  // Tasks posted by the view hold a weak reference to this, so they don't
  // run after the view is destroyed.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterLinuxView);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_FLUTTER_LINUX_VIEW_H_
