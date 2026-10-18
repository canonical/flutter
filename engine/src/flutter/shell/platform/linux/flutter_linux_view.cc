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
  view->window_ =
      WaylandWindow::Create(engine->display(), view.get(), width, height);
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

void FlutterLinuxView::PresentBuffer(DmabufBuffer* buffer) {
  window_->Present(buffer->buffer(), buffer->width(), buffer->height());
}

void FlutterLinuxView::OnWindowConfigure() {
  // Ignore the configure that occurs while the window is being created.
  if (!window_) {
    return;
  }
  SendWindowMetrics();
}

void FlutterLinuxView::OnWindowClose() {
  engine_->OnViewClose(this);
}

}  // namespace flutter
