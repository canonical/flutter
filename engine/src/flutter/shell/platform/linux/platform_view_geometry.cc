// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/platform_view_geometry.h"

#include <algorithm>
#include <cmath>

namespace flutter {

namespace {

// Returns |a| followed by |b|, i.e. the transform that applies |b| then |a|.
FlutterTransformation Multiply(const FlutterTransformation& a,
                               const FlutterTransformation& b) {
  FlutterTransformation r;
  r.scaleX = a.scaleX * b.scaleX + a.skewX * b.skewY + a.transX * b.pers0;
  r.skewX = a.scaleX * b.skewX + a.skewX * b.scaleY + a.transX * b.pers1;
  r.transX = a.scaleX * b.transX + a.skewX * b.transY + a.transX * b.pers2;
  r.skewY = a.skewY * b.scaleX + a.scaleY * b.skewY + a.transY * b.pers0;
  r.scaleY = a.skewY * b.skewX + a.scaleY * b.scaleY + a.transY * b.pers1;
  r.transY = a.skewY * b.transX + a.scaleY * b.transY + a.transY * b.pers2;
  r.pers0 = a.pers0 * b.scaleX + a.pers1 * b.skewY + a.pers2 * b.pers0;
  r.pers1 = a.pers0 * b.skewX + a.pers1 * b.scaleY + a.pers2 * b.pers1;
  r.pers2 = a.pers0 * b.transX + a.pers1 * b.transY + a.pers2 * b.pers2;
  return r;
}

// True if |m| maps rectangles to rectangles without flipping them.
bool PreservesRectangles(const FlutterTransformation& m) {
  return m.skewX == 0 && m.skewY == 0 && m.pers0 == 0 && m.pers1 == 0 &&
         m.pers2 == 1 && m.scaleX > 0 && m.scaleY > 0;
}

// Returns the bounding box of |rect| transformed by |m|.
PixelRect Transform(const FlutterTransformation& m, const PixelRect& rect) {
  const double xs[] = {rect.x, rect.right()};
  const double ys[] = {rect.y, rect.bottom()};
  double min_x = INFINITY, min_y = INFINITY;
  double max_x = -INFINITY, max_y = -INFINITY;
  for (double x : xs) {
    for (double y : ys) {
      double w = m.pers0 * x + m.pers1 * y + m.pers2;
      if (w <= 0) {
        // Behind the viewer, so the bounds are unknown.
        return {-1e9, -1e9, 2e9, 2e9};
      }
      double tx = (m.scaleX * x + m.skewX * y + m.transX) / w;
      double ty = (m.skewY * x + m.scaleY * y + m.transY) / w;
      min_x = std::min(min_x, tx);
      min_y = std::min(min_y, ty);
      max_x = std::max(max_x, tx);
      max_y = std::max(max_y, ty);
    }
  }
  return {min_x, min_y, max_x - min_x, max_y - min_y};
}

PixelRect ToPixelRect(const FlutterRect& rect) {
  return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
}

bool HasRoundedCorners(const FlutterRoundedRect& rect) {
  for (const FlutterSize& radius :
       {rect.upper_left_corner_radius, rect.upper_right_corner_radius,
        rect.lower_right_corner_radius, rect.lower_left_corner_radius}) {
    if (radius.width > 0 && radius.height > 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

PixelRect PixelRect::Intersect(const PixelRect& other) const {
  double left = std::max(x, other.x);
  double top = std::max(y, other.y);
  double r = std::min(right(), other.right());
  double b = std::min(bottom(), other.bottom());
  if (r <= left || b <= top) {
    return {};
  }
  return {left, top, r - left, b - top};
}

PlatformViewGeometry GetPlatformViewGeometry(const FlutterLayer& layer,
                                             double pixel_ratio) {
  PlatformViewGeometry geometry;
  const FlutterPlatformView* view = layer.platform_view;
  if (view->mutations_count == 0) {
    // Not transformed, so the layer is the view's position and size.
    geometry.rect = {layer.offset.x, layer.offset.y, layer.size.width,
                     layer.size.height};
    geometry.visible = geometry.rect;
    return geometry;
  }

  // The mutations are applied from the root of the scene down to the view.
  // Clips are in the coordinate space of the transforms before them.
  FlutterTransformation matrix = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  PixelRect clip = {-1e9, -1e9, 2e9, 2e9};
  for (size_t i = 0; i < view->mutations_count; i++) {
    const FlutterPlatformViewMutation* mutation = view->mutations[i];
    switch (mutation->type) {
      case kFlutterPlatformViewMutationTypeOpacity:
        geometry.opacity *= mutation->opacity;
        break;
      case kFlutterPlatformViewMutationTypeClipRect:
        clip =
            clip.Intersect(Transform(matrix, ToPixelRect(mutation->clip_rect)));
        geometry.approximated |= !PreservesRectangles(matrix);
        break;
      case kFlutterPlatformViewMutationTypeClipRoundedRect:
        clip = clip.Intersect(
            Transform(matrix, ToPixelRect(mutation->clip_rounded_rect.rect)));
        geometry.approximated |= !PreservesRectangles(matrix) ||
                                 HasRoundedCorners(mutation->clip_rounded_rect);
        break;
      case kFlutterPlatformViewMutationTypeTransformation:
        matrix = Multiply(matrix, mutation->transformation);
        break;
    }
  }
  geometry.approximated |= !PreservesRectangles(matrix);

  // The layer size is the view's size in logical pixels scaled by the pixel
  // ratio. The pixel ratio is included in the transforms.
  double ratio = pixel_ratio > 0 ? pixel_ratio : 1.0;
  geometry.rect = Transform(
      matrix, {0, 0, layer.size.width / ratio, layer.size.height / ratio});
  geometry.visible = geometry.rect.Intersect(clip);
  return geometry;
}

bool PlaceBuffer(int buffer_width,
                 int buffer_height,
                 const PixelRect& rect,
                 const PixelRect& visible,
                 double scale,
                 SurfacePlacement* placement) {
  if (buffer_width <= 0 || buffer_height <= 0 || rect.empty() ||
      visible.empty() || scale <= 0) {
    return false;
  }

  // Snap the visible area to logical pixels.
  int32_t x0 = static_cast<int32_t>(std::round(visible.x / scale));
  int32_t y0 = static_cast<int32_t>(std::round(visible.y / scale));
  int32_t x1 = static_cast<int32_t>(std::round(visible.right() / scale));
  int32_t y1 = static_cast<int32_t>(std::round(visible.bottom() / scale));
  if (x1 <= x0 || y1 <= y0) {
    return false;
  }

  // The part of the buffer shown in the snapped area, in 1/256 pixels.
  double sx = buffer_width / rect.width;
  double sy = buffer_height / rect.height;
  auto to_fixed = [](double v) {
    return static_cast<int64_t>(std::round(v * 256.0));
  };
  int64_t max_x = static_cast<int64_t>(buffer_width) * 256;
  int64_t max_y = static_cast<int64_t>(buffer_height) * 256;
  int64_t left =
      std::clamp<int64_t>(to_fixed((x0 * scale - rect.x) * sx), 0, max_x);
  int64_t top =
      std::clamp<int64_t>(to_fixed((y0 * scale - rect.y) * sy), 0, max_y);
  int64_t right =
      std::clamp<int64_t>(to_fixed((x1 * scale - rect.x) * sx), 0, max_x);
  int64_t bottom =
      std::clamp<int64_t>(to_fixed((y1 * scale - rect.y) * sy), 0, max_y);
  if (right <= left || bottom <= top) {
    return false;
  }

  placement->source_x = static_cast<int32_t>(left);
  placement->source_y = static_cast<int32_t>(top);
  placement->source_width = static_cast<int32_t>(right - left);
  placement->source_height = static_cast<int32_t>(bottom - top);
  placement->x = x0;
  placement->y = y0;
  placement->width = x1 - x0;
  placement->height = y1 - y0;
  return true;
}

}  // namespace flutter
