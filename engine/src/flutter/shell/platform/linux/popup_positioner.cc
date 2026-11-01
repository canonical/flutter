// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/popup_positioner.h"

#include <algorithm>

namespace flutter {

namespace {

// The side of a rectangle an xdg_positioner anchor or gravity refers to on
// one axis: -1 for the left or top, 1 for the right or bottom, and 0 for the
// center.
int HorizontalSide(uint32_t value) {
  switch (value) {
    case XDG_POSITIONER_ANCHOR_LEFT:
    case XDG_POSITIONER_ANCHOR_TOP_LEFT:
    case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
      return -1;
    case XDG_POSITIONER_ANCHOR_RIGHT:
    case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
    case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
      return 1;
    default:
      return 0;
  }
}

int VerticalSide(uint32_t value) {
  switch (value) {
    case XDG_POSITIONER_ANCHOR_TOP:
    case XDG_POSITIONER_ANCHOR_TOP_LEFT:
    case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
      return -1;
    case XDG_POSITIONER_ANCHOR_BOTTOM:
    case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
    case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
      return 1;
    default:
      return 0;
  }
}

// Positions the popup on one axis.
int Place(int anchor_side,
          int gravity_side,
          int anchor_start,
          int anchor_length,
          int offset,
          int size) {
  int position = anchor_start + (anchor_side + 1) * anchor_length / 2 + offset;
  if (gravity_side < 0) {
    position -= size;
  } else if (gravity_side == 0) {
    position -= size / 2;
  }
  return position;
}

// Places the popup on one axis, constrained to [bounds_start, bounds_end).
int PlaceAxis(int anchor_side,
              int gravity_side,
              int anchor_start,
              int anchor_length,
              int offset,
              int size,
              int bounds_start,
              int bounds_end,
              bool flip,
              bool slide) {
  auto fits = [&](int position) {
    return position >= bounds_start && position + size <= bounds_end;
  };
  int position = Place(anchor_side, gravity_side, anchor_start, anchor_length,
                       offset, size);
  if (bounds_end <= bounds_start || fits(position)) {
    return position;
  }

  // Flipping inverts the anchor, gravity and offset, and is only used if the
  // popup then fits.
  if (flip) {
    int flipped = Place(-anchor_side, -gravity_side, anchor_start,
                        anchor_length, -offset, size);
    if (fits(flipped)) {
      return flipped;
    }
  }

  // Slide back inside the bounds, keeping the start visible if the popup is
  // larger than them.
  if (slide) {
    if (position + size > bounds_end) {
      position = bounds_end - size;
    }
    if (position < bounds_start) {
      position = bounds_start;
    }
  }
  return position;
}

}  // namespace

void PlacePopup(const Window::Positioner& positioner,
                int width,
                int height,
                const Rect& bounds,
                int* x,
                int* y) {
  uint32_t adjustment = positioner.constraint_adjustment;
  *x = PlaceAxis(
      HorizontalSide(positioner.anchor), HorizontalSide(positioner.gravity),
      positioner.anchor_rect.x, positioner.anchor_rect.width,
      positioner.offset_x, width, bounds.x, bounds.x + bounds.width,
      (adjustment & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X) != 0,
      (adjustment & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X) != 0);
  *y = PlaceAxis(
      VerticalSide(positioner.anchor), VerticalSide(positioner.gravity),
      positioner.anchor_rect.y, positioner.anchor_rect.height,
      positioner.offset_y, height, bounds.y, bounds.y + bounds.height,
      (adjustment & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y) != 0,
      (adjustment & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y) != 0);
}

}  // namespace flutter
