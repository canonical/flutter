// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/platform_view_geometry.h"

#include <vector>

#include "gtest/gtest.h"

namespace flutter {
namespace testing {

namespace {

FlutterTransformation Scale(double x, double y) {
  return {x, 0, 0, 0, y, 0, 0, 0, 1};
}

FlutterTransformation Translate(double x, double y) {
  return {1, 0, x, 0, 1, y, 0, 0, 1};
}

// A platform view layer of |width| x |height| physical pixels with
// |mutations|.
class TestLayer {
 public:
  TestLayer(double width,
            double height,
            std::vector<FlutterPlatformViewMutation> mutations)
      : mutations_(std::move(mutations)) {
    for (const auto& mutation : mutations_) {
      mutation_pointers_.push_back(&mutation);
    }
    view_.struct_size = sizeof(FlutterPlatformView);
    view_.identifier = 1;
    view_.mutations_count = mutation_pointers_.size();
    view_.mutations = mutation_pointers_.data();
    layer_.struct_size = sizeof(FlutterLayer);
    layer_.type = kFlutterLayerContentTypePlatformView;
    layer_.platform_view = &view_;
    layer_.size = {width, height};
  }

  const FlutterLayer& layer() const { return layer_; }

 private:
  std::vector<FlutterPlatformViewMutation> mutations_;
  std::vector<const FlutterPlatformViewMutation*> mutation_pointers_;
  FlutterPlatformView view_ = {};
  FlutterLayer layer_ = {};
};

FlutterPlatformViewMutation Transform(const FlutterTransformation& m) {
  FlutterPlatformViewMutation mutation = {};
  mutation.type = kFlutterPlatformViewMutationTypeTransformation;
  mutation.transformation = m;
  return mutation;
}

FlutterPlatformViewMutation Clip(double l, double t, double r, double b) {
  FlutterPlatformViewMutation mutation = {};
  mutation.type = kFlutterPlatformViewMutationTypeClipRect;
  mutation.clip_rect = {l, t, r, b};
  return mutation;
}

FlutterPlatformViewMutation Opacity(double opacity) {
  FlutterPlatformViewMutation mutation = {};
  mutation.type = kFlutterPlatformViewMutationTypeOpacity;
  mutation.opacity = opacity;
  return mutation;
}

}  // namespace

TEST(PlatformViewGeometryTest, NoMutations) {
  TestLayer test(100, 50, {});
  FlutterLayer layer = test.layer();
  layer.offset = {10, 20};
  PlatformViewGeometry geometry = GetPlatformViewGeometry(layer, 1.0);
  EXPECT_EQ(geometry.rect, (PixelRect{10, 20, 100, 50}));
  EXPECT_EQ(geometry.visible, geometry.rect);
  EXPECT_EQ(geometry.opacity, 1.0);
  EXPECT_FALSE(geometry.approximated);
}

TEST(PlatformViewGeometryTest, PixelRatioAndOffset) {
  // A 100x50 logical view at (10, 20) with a pixel ratio of 2.
  TestLayer test(200, 100,
                 {Transform(Scale(2, 2)), Transform(Translate(10, 20))});
  PlatformViewGeometry geometry = GetPlatformViewGeometry(test.layer(), 2.0);
  EXPECT_EQ(geometry.rect, (PixelRect{20, 40, 200, 100}));
  EXPECT_EQ(geometry.visible, geometry.rect);
  EXPECT_FALSE(geometry.approximated);
}

TEST(PlatformViewGeometryTest, ClipInTransformedSpace) {
  // The clip is in logical pixels, inside the pixel ratio transform.
  TestLayer test(200, 100,
                 {Transform(Scale(2, 2)), Clip(0, 0, 50, 30),
                  Transform(Translate(10, 20))});
  PlatformViewGeometry geometry = GetPlatformViewGeometry(test.layer(), 2.0);
  EXPECT_EQ(geometry.rect, (PixelRect{20, 40, 200, 100}));
  EXPECT_EQ(geometry.visible, (PixelRect{20, 40, 80, 20}));
}

TEST(PlatformViewGeometryTest, ScaledView) {
  TestLayer test(100, 100, {Transform(Scale(1.5, 0.5))});
  PlatformViewGeometry geometry = GetPlatformViewGeometry(test.layer(), 1.0);
  EXPECT_EQ(geometry.rect, (PixelRect{0, 0, 150, 50}));
  EXPECT_FALSE(geometry.approximated);
}

TEST(PlatformViewGeometryTest, Opacity) {
  TestLayer test(10, 10, {Opacity(0.5), Opacity(0.5)});
  PlatformViewGeometry geometry = GetPlatformViewGeometry(test.layer(), 1.0);
  EXPECT_EQ(geometry.opacity, 0.25);
}

TEST(PlatformViewGeometryTest, RotationIsApproximated) {
  // A 90 degree rotation about the origin.
  TestLayer test(100, 50, {Transform({0, -1, 0, 1, 0, 0, 0, 0, 1})});
  PlatformViewGeometry geometry = GetPlatformViewGeometry(test.layer(), 1.0);
  EXPECT_TRUE(geometry.approximated);
  EXPECT_EQ(geometry.rect, (PixelRect{-50, 0, 50, 100}));
}

TEST(PlatformViewGeometryTest, RoundedClipIsApproximated) {
  FlutterPlatformViewMutation clip = {};
  clip.type = kFlutterPlatformViewMutationTypeClipRoundedRect;
  clip.clip_rounded_rect.rect = {0, 0, 50, 50};
  clip.clip_rounded_rect.upper_left_corner_radius = {5, 5};
  TestLayer test(100, 100, {clip});
  PlatformViewGeometry geometry = GetPlatformViewGeometry(test.layer(), 1.0);
  EXPECT_TRUE(geometry.approximated);
  EXPECT_EQ(geometry.visible, (PixelRect{0, 0, 50, 50}));
}

TEST(PlatformViewGeometryTest, FullyClipped) {
  TestLayer test(100, 100, {Clip(200, 200, 300, 300)});
  PlatformViewGeometry geometry = GetPlatformViewGeometry(test.layer(), 1.0);
  EXPECT_TRUE(geometry.visible.empty());
}

TEST(PlatformViewGeometryTest, PlaceUnclipped) {
  SurfacePlacement placement;
  ASSERT_TRUE(PlaceBuffer(640, 480, {20, 40, 200, 100}, {20, 40, 200, 100}, 2.0,
                          &placement));
  EXPECT_EQ(placement,
            (SurfacePlacement{0, 0, 640 * 256, 480 * 256, 10, 20, 100, 50}));
}

TEST(PlatformViewGeometryTest, PlaceClipped) {
  // The right half and top quarter are clipped.
  SurfacePlacement placement;
  ASSERT_TRUE(PlaceBuffer(400, 200, {0, 0, 200, 100}, {0, 25, 100, 75}, 1.0,
                          &placement));
  EXPECT_EQ(placement, (SurfacePlacement{0, 50 * 256, 200 * 256, 150 * 256, 0,
                                         25, 100, 75}));
}

TEST(PlatformViewGeometryTest, PlaceFractionalScale) {
  // Positions are rounded to logical pixels.
  SurfacePlacement placement;
  ASSERT_TRUE(PlaceBuffer(100, 100, {13, 13, 100, 100}, {13, 13, 100, 100},
                          1.25, &placement));
  EXPECT_EQ(placement.x, 10);
  EXPECT_EQ(placement.y, 10);
  EXPECT_EQ(placement.width, 80);
  EXPECT_EQ(placement.height, 80);
  // The snapped area starts half a pixel before the view, so is clamped to
  // the buffer, and ends half a pixel before its right edge.
  EXPECT_EQ(placement.source_x, 0);
  EXPECT_EQ(placement.source_width, 25472);
}

TEST(PlatformViewGeometryTest, PlaceNothingVisible) {
  SurfacePlacement placement;
  EXPECT_FALSE(PlaceBuffer(100, 100, {0, 0, 100, 100}, {}, 1.0, &placement));
  EXPECT_FALSE(PlaceBuffer(100, 100, {0, 0, 100, 100}, {0, 0, 0.2, 100}, 1.0,
                           &placement));
  EXPECT_FALSE(
      PlaceBuffer(0, 100, {0, 0, 100, 100}, {0, 0, 100, 100}, 1.0, &placement));
}

}  // namespace testing
}  // namespace flutter
