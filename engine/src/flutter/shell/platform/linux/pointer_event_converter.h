// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_POINTER_EVENT_CONVERTER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_POINTER_EVENT_CONVERTER_H_

#include <cstdint>
#include <map>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/wayland_seat.h"

namespace flutter {

// Converts Wayland input events on a view into Flutter pointer events.
//
// Mouse buttons and motion are sent as mouse events, wheel scrolling as
// scroll signals, touchpad scrolling and pinching as pan/zoom gestures and
// each touch point as a separate touch device.
class PointerEventConverter : public InputHandler {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // The number of physical pixels per logical pixel in the view.
    virtual double GetPointerScale() = 0;

    virtual void SendPointerEvents(const FlutterPointerEvent* events,
                                   size_t count) = 0;

    // Called when the mouse pointer enters the view.
    virtual void OnPointerEntered() = 0;
  };

  PointerEventConverter(Delegate* delegate, FlutterViewId view_id);

  // |InputHandler|
  void OnPointerEnter(uint64_t time, double x, double y) override;
  void OnPointerLeave(uint64_t time) override;
  void OnPointerMotion(uint64_t time, double x, double y) override;
  void OnPointerButton(uint32_t serial,
                       uint64_t time,
                       uint32_t button,
                       bool pressed) override;
  void OnPointerScroll(uint64_t time, const PointerScroll& scroll) override;
  void OnTouchDown(uint32_t serial,
                   uint64_t time,
                   int32_t id,
                   double x,
                   double y) override;
  void OnTouchUp(uint64_t time, int32_t id) override;
  void OnTouchMotion(uint64_t time, int32_t id, double x, double y) override;
  void OnTouchCancel() override;
  void OnTouchFrame() override;
  void OnPinchBegin(uint64_t time) override;
  void OnPinchUpdate(uint64_t time,
                     double dx,
                     double dy,
                     double scale,
                     double rotation) override;
  void OnPinchEnd(uint64_t time, bool cancelled) override;

 private:
  // Returns an event for the view at a position in logical pixels.
  FlutterPointerEvent MakePointerEvent(FlutterPointerPhase phase,
                                       uint64_t time,
                                       double x,
                                       double y) const;

  // Sends a mouse event at the pointer position.
  void SendMouseEvent(FlutterPointerPhase phase, uint64_t time);

  // Sends a touchpad pan/zoom event at the pointer position.
  void SendPanZoomEvent(FlutterPointerPhase phase, uint64_t time);

  // Queues an event for a touch point, sent on the next touch frame.
  void QueueTouchEvent(FlutterPointerPhase phase,
                       uint64_t time,
                       int32_t device,
                       double x,
                       double y);

  // Returns the device ID for a new touch point.
  int32_t AllocateTouchDevice();

  Delegate* delegate_;
  FlutterViewId view_id_;

  // The mouse pointer, while it is in the view.
  bool pointer_inside_ = false;
  double pointer_x_ = 0;
  double pointer_y_ = 0;
  int64_t buttons_ = 0;

  // Starts a touchpad pan/zoom gesture, ending any in progress.
  void StartPanZoom(uint64_t time, bool pinch);
  void EndPanZoom(uint64_t time);

  // The touchpad scroll or pinch in progress. The pan is in physical pixels
  // and the rotation is clockwise in radians.
  bool pan_active_ = false;
  bool pinch_active_ = false;
  double pan_x_ = 0;
  double pan_y_ = 0;
  double scale_ = 1.0;
  double rotation_ = 0;

  // Touch points in the view.
  struct TouchPoint {
    int32_t device;
    double x;
    double y;
  };
  std::map<int32_t, TouchPoint> touch_points_;
  std::vector<FlutterPointerEvent> touch_events_;

  FML_DISALLOW_COPY_AND_ASSIGN(PointerEventConverter);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_POINTER_EVENT_CONVERTER_H_
