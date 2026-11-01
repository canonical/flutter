// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/popup_positioner.h"

#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

// A menu attached below a 100x20 button at (50, 40).
Window::Positioner MenuPositioner() {
  Window::Positioner positioner;
  positioner.anchor_rect = {50, 40, 100, 20};
  positioner.anchor = Window::Anchor::kBottomLeft;
  positioner.gravity = Window::Anchor::kBottomRight;
  return positioner;
}

}  // namespace

TEST(PopupPositionerTest, AnchorAndGravity) {
  Window::Positioner positioner = MenuPositioner();
  int x = 0, y = 0;
  PlacePopup(positioner, 200, 300, {0, 0, 1000, 1000}, &x, &y);
  EXPECT_EQ(x, 50);
  EXPECT_EQ(y, 60);
}

TEST(PopupPositionerTest, Centered) {
  Window::Positioner positioner;
  positioner.anchor_rect = {100, 100, 50, 50};
  int x = 0, y = 0;
  PlacePopup(positioner, 20, 10, {0, 0, 1000, 1000}, &x, &y);
  EXPECT_EQ(x, 115);
  EXPECT_EQ(y, 120);
}

TEST(PopupPositionerTest, TopRightAndOffset) {
  Window::Positioner positioner;
  positioner.anchor_rect = {100, 100, 50, 50};
  positioner.anchor = Window::Anchor::kTopRight;
  positioner.gravity = Window::Anchor::kTopLeft;
  positioner.offset_x = 5;
  positioner.offset_y = -5;
  int x = 0, y = 0;
  PlacePopup(positioner, 20, 10, {0, 0, 1000, 1000}, &x, &y);
  EXPECT_EQ(x, 135);
  EXPECT_EQ(y, 85);
}

TEST(PopupPositionerTest, UnconstrainedOverflows) {
  Window::Positioner positioner = MenuPositioner();
  int x = 0, y = 0;
  PlacePopup(positioner, 200, 300, {0, 0, 200, 200}, &x, &y);
  EXPECT_EQ(x, 50);
  EXPECT_EQ(y, 60);
}

TEST(PopupPositionerTest, FlipY) {
  Window::Positioner positioner = MenuPositioner();
  positioner.constraint_adjustment = Window::ConstraintAdjustment::kFlipY;
  int x = 0, y = 0;
  PlacePopup(positioner, 100, 30, {0, 0, 1000, 80}, &x, &y);
  EXPECT_EQ(x, 50);
  // Shown above the button instead.
  EXPECT_EQ(y, 10);
}

TEST(PopupPositionerTest, FlipNotUsedIfItDoesNotFit) {
  Window::Positioner positioner = MenuPositioner();
  positioner.constraint_adjustment = Window::ConstraintAdjustment::kFlipY;
  int x = 0, y = 0;
  PlacePopup(positioner, 100, 50, {0, 0, 1000, 80}, &x, &y);
  EXPECT_EQ(y, 60);
}

TEST(PopupPositionerTest, SlideX) {
  Window::Positioner positioner = MenuPositioner();
  positioner.constraint_adjustment = Window::ConstraintAdjustment::kSlideX;
  int x = 0, y = 0;
  PlacePopup(positioner, 200, 30, {0, 0, 220, 1000}, &x, &y);
  EXPECT_EQ(x, 20);
  EXPECT_EQ(y, 60);
}

TEST(PopupPositionerTest, SlideKeepsStartVisible) {
  Window::Positioner positioner = MenuPositioner();
  positioner.constraint_adjustment = Window::ConstraintAdjustment::kSlideX;
  int x = 0, y = 0;
  PlacePopup(positioner, 300, 30, {10, 0, 200, 1000}, &x, &y);
  EXPECT_EQ(x, 10);
}

TEST(PopupPositionerTest, FlipPreferredOverSlide) {
  Window::Positioner positioner = MenuPositioner();
  positioner.constraint_adjustment = Window::ConstraintAdjustment::kFlipY |
                                     Window::ConstraintAdjustment::kSlideY;
  int x = 0, y = 0;
  PlacePopup(positioner, 100, 30, {0, 0, 1000, 80}, &x, &y);
  EXPECT_EQ(y, 10);
}

}  // namespace testing
}  // namespace flutter
