// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_next/flutter_linux_view.h"

#include <cstring>

#include "flutter/shell/platform/linux_next/flutter_linux_engine.h"

namespace flutter {

std::unique_ptr<FlutterLinuxView> FlutterLinuxView::Create(
    FlutterLinuxEngine* engine,
    FlutterViewId view_id,
    const Window::Params& params) {
  std::unique_ptr<FlutterLinuxView> view(new FlutterLinuxView(engine, view_id));
  view->window_ =
      engine->application()->display()->CreateWindow(view.get(), params);
  if (!view->window_) {
    return nullptr;
  }
  return view;
}

FlutterLinuxView::FlutterLinuxView(FlutterLinuxEngine* engine,
                                   FlutterViewId view_id)
    : engine_(engine), view_id_(view_id) {}

FlutterLinuxView::~FlutterLinuxView() {
  // Destroy the window while the view can still act as its delegate.
  window_.reset();
}

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

void FlutterLinuxView::PresentBuffer(const uint8_t* pixels,
                                     int width,
                                     int height,
                                     size_t row_bytes) {
  {
    std::scoped_lock lock(frame_mutex_);
    frame_.resize(row_bytes * height);
    memcpy(frame_.data(), pixels, frame_.size());
    frame_width_ = width;
    frame_height_ = height;
    frame_row_bytes_ = row_bytes;
  }

  engine_->application()->event_loop()->PostTask(
      [this, alive = std::weak_ptr<bool>(alive_)]() {
        if (!alive.expired()) {
          ShowFrame();
        }
      });
}

void FlutterLinuxView::ShowFrame() {
  std::vector<uint8_t> frame;
  int width;
  int height;
  size_t row_bytes;
  {
    std::scoped_lock lock(frame_mutex_);
    if (frame_.empty()) {
      return;
    }
    frame.swap(frame_);
    width = frame_width_;
    height = frame_height_;
    row_bytes = frame_row_bytes_;
  }

  window_->Present(frame.data(), width, height, row_bytes);
}

void FlutterLinuxView::OnWindowConfigure() {
  // Ignore the configure that occurs while the window is being created.
  if (!window_) {
    return;
  }
  SendWindowMetrics();
}

void FlutterLinuxView::OnWindowClose() {
  // Handle the request outside of Wayland event dispatch so the view can be
  // destroyed in response.
  engine_->application()->event_loop()->PostTask(
      [this, alive = std::weak_ptr<bool>(alive_)]() {
        if (!alive.expired()) {
          engine_->OnViewClose(this);
        }
      });
}

}  // namespace flutter
