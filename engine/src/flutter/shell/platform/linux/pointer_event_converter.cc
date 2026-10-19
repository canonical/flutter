// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/pointer_event_converter.h"

#include <linux/input-event-codes.h>

#include <cmath>
#include <ctime>

namespace flutter {

namespace {

// Pointer device IDs. Touch points use IDs from kFirstTouchDevice.
constexpr int32_t kMouseDevice = 0;
constexpr int32_t kPanZoomDevice = 1;
constexpr int32_t kFirstTouchDevice = 2;

// Compositors send 10 units of scrolling per mouse wheel step. Each step
// scrolls 53 logical pixels, as in other Flutter desktop embedders.
constexpr double kScrollUnitsPerStep = 10.0;
constexpr double kScrollPixelsPerStep = 53.0;

// Returns the Flutter button for a Linux input event code, or 0 if it has
// no equivalent.
int64_t ToFlutterButton(uint32_t button) {
  switch (button) {
    case BTN_LEFT:
      return kFlutterPointerButtonMousePrimary;
    case BTN_RIGHT:
      return kFlutterPointerButtonMouseSecondary;
    case BTN_MIDDLE:
      return kFlutterPointerButtonMouseMiddle;
    case BTN_SIDE:
    case BTN_BACK:
      return kFlutterPointerButtonMouseBack;
    case BTN_EXTRA:
    case BTN_FORWARD:
      return kFlutterPointerButtonMouseForward;
    default:
      return 0;
  }
}

}  // namespace

PointerEventConverter::PointerEventConverter(Delegate* delegate,
                                             FlutterViewId view_id)
    : delegate_(delegate), view_id_(view_id) {}

FlutterPointerEvent PointerEventConverter::MakePointerEvent(
    FlutterPointerPhase phase,
    uint64_t time,
    double x,
    double y) const {
  double scale = delegate_->GetPointerScale();
  FlutterPointerEvent event = {};
  event.struct_size = sizeof(FlutterPointerEvent);
  event.phase = phase;
  event.timestamp = time;
  event.x = x * scale;
  event.y = y * scale;
  event.view_id = view_id_;
  return event;
}

void PointerEventConverter::SendMouseEvent(FlutterPointerPhase phase,
                                           uint64_t time) {
  FlutterPointerEvent event =
      MakePointerEvent(phase, time, pointer_x_, pointer_y_);
  event.device = kMouseDevice;
  event.device_kind = kFlutterPointerDeviceKindMouse;
  event.buttons = buttons_;
  delegate_->SendPointerEvents(&event, 1);
}

void PointerEventConverter::SendPanZoomEvent(FlutterPointerPhase phase,
                                             uint64_t time) {
  FlutterPointerEvent event =
      MakePointerEvent(phase, time, pointer_x_, pointer_y_);
  event.device = kPanZoomDevice;
  event.device_kind = kFlutterPointerDeviceKindTrackpad;
  event.pan_x = pan_x_;
  event.pan_y = pan_y_;
  event.scale = scale_;
  event.rotation = rotation_;
  delegate_->SendPointerEvents(&event, 1);
}

void PointerEventConverter::StartPanZoom(uint64_t time, bool pinch) {
  EndPanZoom(time);
  pan_active_ = true;
  pinch_active_ = pinch;
  pan_x_ = 0;
  pan_y_ = 0;
  scale_ = 1.0;
  rotation_ = 0;
  SendPanZoomEvent(kPanZoomStart, time);
}

void PointerEventConverter::EndPanZoom(uint64_t time) {
  if (!pan_active_) {
    return;
  }
  pan_active_ = false;
  pinch_active_ = false;
  SendPanZoomEvent(kPanZoomEnd, time);
}

void PointerEventConverter::OnPointerEnter(uint64_t time, double x, double y) {
  pointer_inside_ = true;
  pointer_x_ = x;
  pointer_y_ = y;
  buttons_ = 0;
  SendMouseEvent(kAdd, time);
  delegate_->OnPointerEntered();
}

void PointerEventConverter::OnPointerLeave(uint64_t time) {
  if (!pointer_inside_) {
    return;
  }
  EndPanZoom(time);
  // The engine cancels any press in progress.
  SendMouseEvent(kRemove, time);
  pointer_inside_ = false;
  buttons_ = 0;
}

void PointerEventConverter::OnPointerMotion(uint64_t time, double x, double y) {
  if (!pointer_inside_) {
    return;
  }
  pointer_x_ = x;
  pointer_y_ = y;
  SendMouseEvent(buttons_ != 0 ? kMove : kHover, time);
}

void PointerEventConverter::OnPointerButton(uint32_t serial,
                                            uint64_t time,
                                            uint32_t button,
                                            bool pressed) {
  int64_t flutter_button = ToFlutterButton(button);
  if (!pointer_inside_ || flutter_button == 0) {
    return;
  }
  FlutterPointerPhase phase;
  if (pressed) {
    if ((buttons_ & flutter_button) != 0) {
      return;
    }
    phase = buttons_ == 0 ? kDown : kMove;
    buttons_ |= flutter_button;
  } else {
    if ((buttons_ & flutter_button) == 0) {
      return;
    }
    buttons_ &= ~flutter_button;
    phase = buttons_ == 0 ? kUp : kMove;
  }
  SendMouseEvent(phase, time);
}

void PointerEventConverter::OnPointerScroll(uint64_t time,
                                            const PointerScroll& scroll) {
  if (!pointer_inside_) {
    return;
  }
  double pixels_per_unit =
      kScrollPixelsPerStep / kScrollUnitsPerStep * delegate_->GetPointerScale();
  double dx = scroll.dx * pixels_per_unit;
  double dy = scroll.dy * pixels_per_unit;

  // Touchpads pan the content, which allows Flutter to fling it when the
  // fingers are lifted.
  if (scroll.source == WL_POINTER_AXIS_SOURCE_FINGER) {
    // Pinches include their own motion.
    if (pinch_active_) {
      return;
    }
    if (!pan_active_ && !scroll.stop) {
      StartPanZoom(time, false);
    }
    if (!pan_active_) {
      return;
    }
    if (dx != 0 || dy != 0) {
      pan_x_ -= dx;
      pan_y_ -= dy;
      SendPanZoomEvent(kPanZoomUpdate, time);
    }
    if (scroll.stop) {
      EndPanZoom(time);
    }
    return;
  }

  if (dx == 0 && dy == 0) {
    return;
  }
  FlutterPointerEvent event = MakePointerEvent(buttons_ != 0 ? kMove : kHover,
                                               time, pointer_x_, pointer_y_);
  event.device = kMouseDevice;
  event.device_kind = kFlutterPointerDeviceKindMouse;
  event.buttons = buttons_;
  event.signal_kind = kFlutterPointerSignalKindScroll;
  event.scroll_delta_x = dx;
  event.scroll_delta_y = dy;
  delegate_->SendPointerEvents(&event, 1);
}

int32_t PointerEventConverter::AllocateTouchDevice() {
  int32_t device = kFirstTouchDevice;
  bool used = true;
  while (used) {
    used = false;
    for (const auto& [id, point] : touch_points_) {
      if (point.device == device) {
        used = true;
        device++;
        break;
      }
    }
  }
  return device;
}

void PointerEventConverter::QueueTouchEvent(FlutterPointerPhase phase,
                                            uint64_t time,
                                            int32_t device,
                                            double x,
                                            double y) {
  FlutterPointerEvent event = MakePointerEvent(phase, time, x, y);
  event.device = device;
  event.device_kind = kFlutterPointerDeviceKindTouch;
  touch_events_.push_back(event);
}

void PointerEventConverter::OnTouchDown(uint32_t serial,
                                        uint64_t time,
                                        int32_t id,
                                        double x,
                                        double y) {
  if (touch_points_.count(id) != 0) {
    return;
  }
  int32_t device = AllocateTouchDevice();
  touch_points_[id] = {device, x, y};
  QueueTouchEvent(kAdd, time, device, x, y);
  QueueTouchEvent(kDown, time, device, x, y);
}

void PointerEventConverter::OnTouchUp(uint64_t time, int32_t id) {
  auto it = touch_points_.find(id);
  if (it == touch_points_.end()) {
    return;
  }
  TouchPoint point = it->second;
  touch_points_.erase(it);
  QueueTouchEvent(kUp, time, point.device, point.x, point.y);
  QueueTouchEvent(kRemove, time, point.device, point.x, point.y);
}

void PointerEventConverter::OnTouchMotion(uint64_t time,
                                          int32_t id,
                                          double x,
                                          double y) {
  auto it = touch_points_.find(id);
  if (it == touch_points_.end()) {
    return;
  }
  it->second.x = x;
  it->second.y = y;
  QueueTouchEvent(kMove, time, it->second.device, x, y);
}

void PointerEventConverter::OnTouchCancel() {
  uint64_t time = touch_events_.empty() ? 0 : touch_events_.back().timestamp;
  if (time == 0) {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    time = static_cast<uint64_t>(now.tv_sec) * 1000000 + now.tv_nsec / 1000;
  }
  for (const auto& [id, point] : touch_points_) {
    QueueTouchEvent(kCancel, time, point.device, point.x, point.y);
    QueueTouchEvent(kRemove, time, point.device, point.x, point.y);
  }
  touch_points_.clear();
  OnTouchFrame();
}

void PointerEventConverter::OnTouchFrame() {
  delegate_->SendPointerEvents(touch_events_.data(), touch_events_.size());
  touch_events_.clear();
}

void PointerEventConverter::OnPinchBegin(uint64_t time) {
  if (!pointer_inside_) {
    return;
  }
  StartPanZoom(time, true);
}

void PointerEventConverter::OnPinchUpdate(uint64_t time,
                                          double dx,
                                          double dy,
                                          double scale,
                                          double rotation) {
  if (!pinch_active_) {
    return;
  }
  double pixel_scale = delegate_->GetPointerScale();
  pan_x_ += dx * pixel_scale;
  pan_y_ += dy * pixel_scale;
  scale_ = scale;
  rotation_ += rotation * M_PI / 180.0;
  SendPanZoomEvent(kPanZoomUpdate, time);
}

void PointerEventConverter::OnPinchEnd(uint64_t time, bool cancelled) {
  if (pinch_active_) {
    EndPanZoom(time);
  }
}

}  // namespace flutter
