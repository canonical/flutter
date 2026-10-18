// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_VSYNC_PREDICTOR_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_VSYNC_PREDICTOR_H_

#include <cstdint>

namespace flutter {

// Predicts when the display will next refresh, using the times frames were
// shown on the screen as reported by wp_presentation.
//
// All times are in nanoseconds.
class VsyncPredictor {
 public:
  // The refresh interval to assume if the compositor doesn't report one.
  static constexpr uint64_t kDefaultRefreshInterval = 1000000000 / 60;

  struct Vsync {
    // When the frame should start being produced.
    uint64_t start_time;

    // When the frame should be ready to be shown.
    uint64_t target_time;
  };

  // Records that a frame was shown at |presentation_time|. |refresh_interval|
  // is the time until the next display refresh, or zero if unknown.
  void OnFramePresented(uint64_t presentation_time, uint64_t refresh_interval);

  // Returns the next vsync at or after |now|. At most one vsync is returned
  // per refresh interval, as more frames than that would not be shown.
  Vsync NextVsync(uint64_t now);

 private:
  uint64_t last_presentation_time_ = 0;
  uint64_t refresh_interval_ = 0;
  uint64_t last_vsync_time_ = 0;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_VSYNC_PREDICTOR_H_
