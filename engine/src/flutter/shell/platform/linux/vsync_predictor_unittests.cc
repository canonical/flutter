// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/vsync_predictor.h"

#include "gtest/gtest.h"

namespace flutter {
namespace testing {

constexpr uint64_t kInterval = 10000;

TEST(VsyncPredictorTest, StartsImmediatelyBeforeFirstPresentation) {
  VsyncPredictor predictor;
  VsyncPredictor::Vsync vsync = predictor.NextVsync(1000);
  EXPECT_EQ(vsync.start_time, 1000u);
  EXPECT_EQ(vsync.target_time, 1000u + VsyncPredictor::kDefaultRefreshInterval);
}

TEST(VsyncPredictorTest, LimitsRateBeforeFirstPresentation) {
  VsyncPredictor predictor;
  predictor.NextVsync(1000);
  EXPECT_EQ(predictor.NextVsync(1001).start_time,
            1000u + VsyncPredictor::kDefaultRefreshInterval);
}

TEST(VsyncPredictorTest, AlignsToPresentation) {
  VsyncPredictor predictor;
  predictor.OnFramePresented(100000, kInterval);
  VsyncPredictor::Vsync vsync = predictor.NextVsync(103000);
  EXPECT_EQ(vsync.start_time, 110000u);
  EXPECT_EQ(vsync.target_time, 120000u);
}

TEST(VsyncPredictorTest, AlignsToOldPresentation) {
  VsyncPredictor predictor;
  predictor.OnFramePresented(100000, kInterval);
  EXPECT_EQ(predictor.NextVsync(1234567).start_time, 1240000u);
}

TEST(VsyncPredictorTest, VsyncOnRefreshIsImmediate) {
  VsyncPredictor predictor;
  predictor.OnFramePresented(100000, kInterval);
  EXPECT_EQ(predictor.NextVsync(120000).start_time, 120000u);
}

TEST(VsyncPredictorTest, PresentationInFuture) {
  VsyncPredictor predictor;
  predictor.OnFramePresented(100000, kInterval);
  EXPECT_EQ(predictor.NextVsync(95000).start_time, 100000u);
}

TEST(VsyncPredictorTest, OneVsyncPerRefresh) {
  VsyncPredictor predictor;
  predictor.OnFramePresented(100000, kInterval);
  EXPECT_EQ(predictor.NextVsync(103000).start_time, 110000u);
  // The frame was drawn and shown before the next refresh.
  predictor.OnFramePresented(110000, kInterval);
  EXPECT_EQ(predictor.NextVsync(110500).start_time, 120000u);
}

TEST(VsyncPredictorTest, UnknownRefreshKeepsPreviousInterval) {
  VsyncPredictor predictor;
  predictor.OnFramePresented(100000, kInterval);
  predictor.OnFramePresented(110000, 0);
  EXPECT_EQ(predictor.NextVsync(113000).start_time, 120000u);
}

TEST(VsyncPredictorTest, IgnoresOutOfOrderPresentation) {
  VsyncPredictor predictor;
  predictor.OnFramePresented(100000, kInterval);
  predictor.OnFramePresented(95000, kInterval);
  EXPECT_EQ(predictor.NextVsync(103000).start_time, 110000u);
}

}  // namespace testing
}  // namespace flutter
