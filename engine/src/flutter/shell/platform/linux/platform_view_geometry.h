// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_VIEW_GEOMETRY_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_VIEW_GEOMETRY_H_

#include <cstdint>

#include "flutter/shell/platform/embedder/embedder.h"

namespace flutter {

// A rectangle in logical pixels.
struct LogicalRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// A rectangle in physical pixels.
struct PixelRect {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;

  double right() const { return x + width; }
  double bottom() const { return y + height; }
  bool empty() const { return width <= 0 || height <= 0; }

  // Returns the part of this rectangle inside |other|.
  PixelRect Intersect(const PixelRect& other) const;

  bool operator==(const PixelRect& other) const = default;
};

// Where a platform view is shown in a Flutter view.
struct PlatformViewGeometry {
  // The platform view's content, in physical pixels within the view.
  PixelRect rect;

  // The part of |rect| that is visible after clipping. Empty if the view is
  // completely clipped.
  PixelRect visible;

  double opacity = 1.0;

  // True if the platform view is transformed or clipped in a way that can't
  // be shown exactly (rotations, skews, perspective and rounded clips), so
  // its bounding box is used.
  bool approximated = false;
};

// Works out where the platform view in |layer| is shown. |pixel_ratio| is the
// view's device pixel ratio.
PlatformViewGeometry GetPlatformViewGeometry(const FlutterLayer& layer,
                                             double pixel_ratio);

// The placement of a buffer on a Wayland surface: the part of the buffer
// shown (in buffer pixels, after the buffer transform) and where it is shown
// (in logical pixels).
struct SurfacePlacement {
  // The source rectangle, in 1/256 pixel units (wl_fixed_t).
  int32_t source_x = 0;
  int32_t source_y = 0;
  int32_t source_width = 0;
  int32_t source_height = 0;

  // The position relative to the parent surface and size, in logical pixels.
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;

  bool operator==(const SurfacePlacement& other) const = default;
};

// Places a |buffer_width| x |buffer_height| buffer so its full contents fill
// |rect|, showing only the part in |visible|. Rectangles are in physical
// pixels and |scale| is the number of physical pixels per logical pixel.
// Positions are rounded to logical pixels, as subsurfaces can't be placed
// more precisely. Returns false if nothing is visible.
bool PlaceBuffer(int buffer_width,
                 int buffer_height,
                 const PixelRect& rect,
                 const PixelRect& visible,
                 double scale,
                 SurfacePlacement* placement);

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_PLATFORM_VIEW_GEOMETRY_H_
