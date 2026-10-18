// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/flutter_linux_view.h"

#include "flutter/shell/platform/linux/flutter_linux_engine.h"

namespace flutter {

std::unique_ptr<FlutterLinuxView> FlutterLinuxView::Create(
    FlutterLinuxEngine* engine,
    FlutterViewId view_id,
    int width,
    int height) {
  std::unique_ptr<FlutterLinuxView> view(new FlutterLinuxView(engine, view_id));
  view->window_ = WaylandWindow::Create(engine->application()->display(),
                                        view.get(), width, height);
  if (!view->window_) {
    return nullptr;
  }
  return view;
}

FlutterLinuxView::FlutterLinuxView(FlutterLinuxEngine* engine,
                                   FlutterViewId view_id)
    : engine_(engine), view_id_(view_id) {}

FlutterLinuxView::~FlutterLinuxView() = default;

FlutterWindowMetricsEvent FlutterLinuxView::GetWindowMetrics() const {
  FlutterWindowMetricsEvent event = {};
  event.struct_size = sizeof(FlutterWindowMetricsEvent);
  event.width = window_->physical_width();
  event.height = window_->physical_height();
  event.pixel_ratio = window_->scale();
  event.view_id = view_id_;
  return event;
}

void FlutterLinuxView::SendWindowMetrics() {
  engine_->SendWindowMetrics(GetWindowMetrics());
}

void FlutterLinuxView::PresentBuffer(RenderBuffer* buffer,
                                     const WaylandWindow::SyncPoints* sync) {
  window_->Present(buffer->buffer(), buffer->width(), buffer->height(), sync);
}

void FlutterLinuxView::OnWindowConfigure() {
  // Ignore the configure that occurs while the window is being created.
  if (!window_) {
    return;
  }
  SendWindowMetrics();
}

void FlutterLinuxView::OnWindowClose() {
  // Handle the request outside of Wayland event dispatch so the callback can
  // destroy the view.
  engine_->application()->event_loop()->PostTask(
      [this, alive = std::weak_ptr<bool>(alive_)]() {
        if (alive.expired()) {
          return;
        }
        if (close_callback_) {
          // Copy the callback as it may destroy the view.
          std::function<void()> callback = close_callback_;
          callback();
          return;
        }
        engine_->OnViewClose(this);
      });
}

void FlutterLinuxView::OnWindowPresented(uint64_t presentation_time_ns,
                                         uint64_t refresh_ns) {
  engine_->OnFramePresented(presentation_time_ns, refresh_ns);
}

void FlutterLinuxView::OnWindowFrameDone() {
  engine_->OnFrameDone();
}

}  // namespace flutter
