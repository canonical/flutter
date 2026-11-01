// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_DRM_TIMELINE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_DRM_TIMELINE_H_

#include <cstdint>
#include <memory>

#include "flutter/fml/macros.h"
#include "flutter/fml/unique_fd.h"
#include "flutter/shell/platform/linux/wayland/protocols/linux-drm-syncobj-v1-client-protocol.h"

namespace flutter {

// A DRM timeline synchronization object shared with the Wayland compositor
// for explicit synchronization.
//
// Each point on the timeline is signalled by a fence. Signalling a point also
// signals all previous points.
class DrmTimeline {
 public:
  // Creates a timeline on |drm_fd| and imports it into the compositor using
  // |manager|. Returns nullptr on failure.
  static std::unique_ptr<DrmTimeline> Create(
      int drm_fd,
      wp_linux_drm_syncobj_manager_v1* manager);

  ~DrmTimeline();

  // The compositor's handle for this timeline.
  wp_linux_drm_syncobj_timeline_v1* timeline() const { return timeline_; }

  // Sets |point| to be signalled when |sync_file| signals.
  bool ImportSyncFile(uint64_t point, const fml::UniqueFD& sync_file);

  // Signals |point| immediately.
  bool Signal(uint64_t point);

  // True if a fence has been set for |point|. The fence may not have
  // signalled yet.
  bool IsAvailable(uint64_t point) const;

  // Returns a sync file that signals when |point| signals. |point| must be
  // available.
  fml::UniqueFD ExportSyncFile(uint64_t point);

  // Waits for |point| to signal, until |deadline_ns| on CLOCK_MONOTONIC.
  bool Wait(uint64_t point, int64_t deadline_ns) const;

  // Waits until |point| for any of the |count| |timelines| is available,
  // until |deadline_ns| on CLOCK_MONOTONIC. All timelines must be on the
  // same DRM device. Returns false on timeout or failure.
  static bool WaitAnyAvailable(DrmTimeline* const* timelines,
                               const uint64_t* points,
                               size_t count,
                               int64_t deadline_ns);

 private:
  explicit DrmTimeline(int drm_fd);

  bool Initialize(wp_linux_drm_syncobj_manager_v1* manager);

  int drm_fd_;

  // The timeline syncobj.
  uint32_t handle_ = 0;

  // A binary syncobj used to move fences between sync files and timeline
  // points, as the kernel only converts sync files to binary syncobjs.
  uint32_t binary_handle_ = 0;

  wp_linux_drm_syncobj_timeline_v1* timeline_ = nullptr;

  FML_DISALLOW_COPY_AND_ASSIGN(DrmTimeline);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_DRM_TIMELINE_H_
