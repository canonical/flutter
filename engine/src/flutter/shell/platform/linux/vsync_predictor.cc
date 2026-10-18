// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/vsync_predictor.h"

namespace flutter {

void VsyncPredictor::OnFramePresented(uint64_t presentation_time,
                                      uint64_t refresh_interval) {
  // Ignore results that arrive out of order, e.g. from multiple views.
  if (presentation_time < last_presentation_time_) {
    return;
  }
  last_presentation_time_ = presentation_time;
  if (refresh_interval != 0) {
    refresh_interval_ = refresh_interval;
  }
}

VsyncPredictor::Vsync VsyncPredictor::NextVsync(uint64_t now) {
  uint64_t interval = refresh_interval();

  // Predict the next display refresh from the last time a frame was shown.
  // Before the first frame is shown, start immediately.
  uint64_t vsync_time = now;
  if (last_presentation_time_ != 0) {
    if (last_presentation_time_ >= now) {
      vsync_time = last_presentation_time_;
    } else {
      uint64_t intervals =
          (now - last_presentation_time_ + interval - 1) / interval;
      vsync_time = last_presentation_time_ + intervals * interval;
    }
  }

  // Frames drawn faster than the display refreshes would be discarded by the
  // compositor. This occurs before the first frame is shown, or if a frame is
  // drawn and shown within one refresh.
  if (last_vsync_time_ != 0) {
    uint64_t earliest = last_vsync_time_ + interval / 2;
    if (vsync_time < earliest) {
      if (last_presentation_time_ == 0) {
        // Continue the timeline started by the previous vsync.
        vsync_time = last_vsync_time_ + interval;
      } else {
        while (vsync_time < earliest) {
          vsync_time += interval;
        }
      }
    }
  }
  last_vsync_time_ = vsync_time;

  return {vsync_time, vsync_time + interval};
}

}  // namespace flutter
