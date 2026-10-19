// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/pointer_event_converter.h"

#include <linux/input-event-codes.h>

#include <cmath>
#include <vector>

#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

constexpr FlutterViewId kViewId = 7;

class TestDelegate : public PointerEventConverter::Delegate {
 public:
  double GetPointerScale() override { return scale; }

  void SendPointerEvents(const FlutterPointerEvent* new_events,
                         size_t count) override {
    batches.push_back(count);
    events.insert(events.end(), new_events, new_events + count);
  }

  void OnPointerEntered() override { entered++; }

  double scale = 1.0;
  std::vector<FlutterPointerEvent> events;
  std::vector<size_t> batches;
  int entered = 0;
};

}  // namespace

TEST(PointerEventConverterTest, MouseClick) {
  TestDelegate delegate;
  delegate.scale = 2.0;
  PointerEventConverter converter(&delegate, kViewId);

  converter.OnPointerEnter(1000, 10, 20);
  converter.OnPointerMotion(2000, 11, 21);
  converter.OnPointerButton(1, 3000, BTN_LEFT, true);
  converter.OnPointerButton(2, 4000, BTN_RIGHT, true);
  converter.OnPointerMotion(5000, 12, 22);
  converter.OnPointerButton(3, 6000, BTN_LEFT, false);
  converter.OnPointerButton(4, 7000, BTN_RIGHT, false);
  converter.OnPointerLeave(8000);

  EXPECT_EQ(delegate.entered, 1);
  ASSERT_EQ(delegate.events.size(), 8u);
  const FlutterPointerPhase phases[] = {kAdd,  kHover, kDown, kMove,
                                        kMove, kMove,  kUp,   kRemove};
  const int64_t buttons[] = {
      0,
      0,
      kFlutterPointerButtonMousePrimary,
      kFlutterPointerButtonMousePrimary | kFlutterPointerButtonMouseSecondary,
      kFlutterPointerButtonMousePrimary | kFlutterPointerButtonMouseSecondary,
      kFlutterPointerButtonMouseSecondary,
      0,
      0};
  for (size_t i = 0; i < delegate.events.size(); i++) {
    const FlutterPointerEvent& event = delegate.events[i];
    EXPECT_EQ(event.phase, phases[i]) << i;
    EXPECT_EQ(event.buttons, buttons[i]) << i;
    EXPECT_EQ(event.timestamp, (i + 1) * 1000) << i;
    EXPECT_EQ(event.device_kind, kFlutterPointerDeviceKindMouse);
    EXPECT_EQ(event.view_id, kViewId);
    EXPECT_EQ(event.struct_size, sizeof(FlutterPointerEvent));
  }
  EXPECT_EQ(delegate.events[0].x, 20);
  EXPECT_EQ(delegate.events[0].y, 40);
  EXPECT_EQ(delegate.events[4].x, 24);
  EXPECT_EQ(delegate.events[4].y, 44);
}

TEST(PointerEventConverterTest, IgnoresUnknownAndRepeatedButtons) {
  TestDelegate delegate;
  PointerEventConverter converter(&delegate, kViewId);

  converter.OnPointerEnter(0, 0, 0);
  converter.OnPointerButton(1, 0, BTN_TASK, true);
  converter.OnPointerButton(2, 0, BTN_LEFT, false);
  converter.OnPointerButton(3, 0, BTN_SIDE, true);
  converter.OnPointerButton(4, 0, BTN_SIDE, true);
  converter.OnPointerButton(5, 0, BTN_SIDE, false);

  ASSERT_EQ(delegate.events.size(), 3u);
  EXPECT_EQ(delegate.events[1].phase, kDown);
  EXPECT_EQ(delegate.events[1].buttons, kFlutterPointerButtonMouseBack);
  EXPECT_EQ(delegate.events[2].phase, kUp);
}

TEST(PointerEventConverterTest, IgnoresEventsOutsideView) {
  TestDelegate delegate;
  PointerEventConverter converter(&delegate, kViewId);

  converter.OnPointerMotion(0, 1, 1);
  converter.OnPointerButton(1, 0, BTN_LEFT, true);
  converter.OnPointerScroll(0, {.dy = 10});
  converter.OnPointerLeave(0);

  EXPECT_TRUE(delegate.events.empty());
}

TEST(PointerEventConverterTest, WheelScroll) {
  TestDelegate delegate;
  delegate.scale = 2.0;
  PointerEventConverter converter(&delegate, kViewId);

  converter.OnPointerEnter(0, 5, 6);
  converter.OnPointerScroll(
      1000, {.source = WL_POINTER_AXIS_SOURCE_WHEEL, .dx = -10, .dy = 10});

  ASSERT_EQ(delegate.events.size(), 2u);
  const FlutterPointerEvent& event = delegate.events[1];
  EXPECT_EQ(event.phase, kHover);
  EXPECT_EQ(event.signal_kind, kFlutterPointerSignalKindScroll);
  EXPECT_EQ(event.device_kind, kFlutterPointerDeviceKindMouse);
  EXPECT_DOUBLE_EQ(event.scroll_delta_x, -106);
  EXPECT_DOUBLE_EQ(event.scroll_delta_y, 106);
  EXPECT_EQ(event.x, 10);
  EXPECT_EQ(event.y, 12);
}

TEST(PointerEventConverterTest, TouchpadPan) {
  TestDelegate delegate;
  PointerEventConverter converter(&delegate, kViewId);

  converter.OnPointerEnter(0, 0, 0);
  PointerScroll scroll = {.source = WL_POINTER_AXIS_SOURCE_FINGER, .dy = 10};
  converter.OnPointerScroll(1000, scroll);
  converter.OnPointerScroll(2000, scroll);
  converter.OnPointerScroll(
      3000, {.source = WL_POINTER_AXIS_SOURCE_FINGER, .stop = true});
  // A stop without a pan in progress is ignored.
  converter.OnPointerScroll(
      4000, {.source = WL_POINTER_AXIS_SOURCE_FINGER, .stop = true});

  ASSERT_EQ(delegate.events.size(), 5u);
  EXPECT_EQ(delegate.events[1].phase, kPanZoomStart);
  EXPECT_EQ(delegate.events[2].phase, kPanZoomUpdate);
  EXPECT_DOUBLE_EQ(delegate.events[2].pan_y, -53);
  EXPECT_EQ(delegate.events[3].phase, kPanZoomUpdate);
  EXPECT_DOUBLE_EQ(delegate.events[3].pan_y, -106);
  EXPECT_DOUBLE_EQ(delegate.events[3].scale, 1.0);
  EXPECT_EQ(delegate.events[4].phase, kPanZoomEnd);
  for (size_t i = 1; i < delegate.events.size(); i++) {
    EXPECT_EQ(delegate.events[i].device_kind,
              kFlutterPointerDeviceKindTrackpad);
    EXPECT_NE(delegate.events[i].device, delegate.events[0].device);
  }
}

TEST(PointerEventConverterTest, LeaveEndsPan) {
  TestDelegate delegate;
  PointerEventConverter converter(&delegate, kViewId);

  converter.OnPointerEnter(0, 0, 0);
  converter.OnPointerScroll(1000,
                            {.source = WL_POINTER_AXIS_SOURCE_FINGER, .dx = 1});
  converter.OnPointerLeave(2000);

  ASSERT_EQ(delegate.events.size(), 5u);
  EXPECT_EQ(delegate.events[3].phase, kPanZoomEnd);
  EXPECT_EQ(delegate.events[4].phase, kRemove);
}

TEST(PointerEventConverterTest, TouchpadPinch) {
  TestDelegate delegate;
  delegate.scale = 2.0;
  PointerEventConverter converter(&delegate, kViewId);

  converter.OnPointerEnter(0, 10, 20);
  converter.OnPinchBegin(1000);
  converter.OnPinchUpdate(2000, 1, 2, 1.5, 90);
  // Scrolling during a pinch is ignored.
  converter.OnPointerScroll(2500,
                            {.source = WL_POINTER_AXIS_SOURCE_FINGER, .dx = 1});
  converter.OnPinchUpdate(3000, 1, 0, 2.0, -45);
  converter.OnPinchEnd(4000, false);
  // Updates after the end are ignored.
  converter.OnPinchUpdate(5000, 1, 0, 2.0, 0);

  ASSERT_EQ(delegate.events.size(), 5u);
  EXPECT_EQ(delegate.events[1].phase, kPanZoomStart);
  EXPECT_DOUBLE_EQ(delegate.events[1].x, 20);
  EXPECT_DOUBLE_EQ(delegate.events[1].y, 40);
  EXPECT_EQ(delegate.events[2].phase, kPanZoomUpdate);
  EXPECT_DOUBLE_EQ(delegate.events[2].pan_x, 2);
  EXPECT_DOUBLE_EQ(delegate.events[2].pan_y, 4);
  EXPECT_DOUBLE_EQ(delegate.events[2].scale, 1.5);
  EXPECT_DOUBLE_EQ(delegate.events[2].rotation, M_PI / 2);
  EXPECT_EQ(delegate.events[3].phase, kPanZoomUpdate);
  EXPECT_DOUBLE_EQ(delegate.events[3].pan_x, 4);
  EXPECT_DOUBLE_EQ(delegate.events[3].scale, 2.0);
  EXPECT_DOUBLE_EQ(delegate.events[3].rotation, M_PI / 4);
  EXPECT_EQ(delegate.events[4].phase, kPanZoomEnd);
  EXPECT_EQ(delegate.events[4].device_kind, kFlutterPointerDeviceKindTrackpad);
}

TEST(PointerEventConverterTest, PinchEndsPan) {
  TestDelegate delegate;
  PointerEventConverter converter(&delegate, kViewId);

  converter.OnPointerEnter(0, 0, 0);
  converter.OnPointerScroll(1000,
                            {.source = WL_POINTER_AXIS_SOURCE_FINGER, .dx = 1});
  converter.OnPinchBegin(2000);
  converter.OnPinchEnd(3000, true);

  ASSERT_EQ(delegate.events.size(), 6u);
  EXPECT_EQ(delegate.events[3].phase, kPanZoomEnd);
  EXPECT_EQ(delegate.events[4].phase, kPanZoomStart);
  EXPECT_DOUBLE_EQ(delegate.events[4].scale, 1.0);
  EXPECT_EQ(delegate.events[5].phase, kPanZoomEnd);
}

TEST(PointerEventConverterTest, MultiTouch) {
  TestDelegate delegate;
  delegate.scale = 2.0;
  PointerEventConverter converter(&delegate, kViewId);

  converter.OnTouchDown(1, 1000, 5, 10, 10);
  converter.OnTouchDown(2, 1000, 9, 20, 20);
  EXPECT_TRUE(delegate.events.empty());
  converter.OnTouchFrame();
  converter.OnTouchMotion(2000, 5, 11, 12);
  converter.OnTouchMotion(2000, 9, 21, 22);
  converter.OnTouchFrame();
  converter.OnTouchUp(3000, 5);
  converter.OnTouchFrame();
  // The first touch's device is reused.
  converter.OnTouchDown(3, 4000, 6, 30, 30);
  converter.OnTouchUp(5000, 9);
  converter.OnTouchFrame();

  EXPECT_EQ(delegate.batches, (std::vector<size_t>{4, 2, 2, 4}));
  ASSERT_EQ(delegate.events.size(), 12u);
  const FlutterPointerPhase phases[] = {kAdd,  kDown, kAdd, kDown,
                                        kMove, kMove, kUp,  kRemove,
                                        kAdd,  kDown, kUp,  kRemove};
  const int32_t devices[] = {2, 2, 3, 3, 2, 3, 2, 2, 2, 2, 3, 3};
  for (size_t i = 0; i < delegate.events.size(); i++) {
    EXPECT_EQ(delegate.events[i].phase, phases[i]) << i;
    EXPECT_EQ(delegate.events[i].device, devices[i]) << i;
    EXPECT_EQ(delegate.events[i].device_kind, kFlutterPointerDeviceKindTouch);
  }
  EXPECT_EQ(delegate.events[4].x, 22);
  EXPECT_EQ(delegate.events[4].y, 24);
  // Up events are at the last position.
  EXPECT_EQ(delegate.events[6].x, 22);
  EXPECT_EQ(delegate.events[6].y, 24);
  EXPECT_EQ(delegate.events[10].x, 42);
}

TEST(PointerEventConverterTest, TouchCancel) {
  TestDelegate delegate;
  PointerEventConverter converter(&delegate, kViewId);

  converter.OnTouchDown(1, 1000, 1, 10, 10);
  converter.OnTouchFrame();
  converter.OnTouchMotion(2000, 1, 11, 11);
  converter.OnTouchCancel();
  // The touch point is gone.
  converter.OnTouchUp(3000, 1);
  converter.OnTouchFrame();

  ASSERT_EQ(delegate.events.size(), 5u);
  EXPECT_EQ(delegate.events[2].phase, kMove);
  EXPECT_EQ(delegate.events[3].phase, kCancel);
  EXPECT_EQ(delegate.events[4].phase, kRemove);
  EXPECT_EQ(delegate.events[4].timestamp, 2000u);
}

}  // namespace testing
}  // namespace flutter
