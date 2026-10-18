// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/drm_timeline.h"

#include <xf86drm.h>

#include <cerrno>
#include <cstring>
#include <vector>

#include "flutter/fml/logging.h"

namespace flutter {

std::unique_ptr<DrmTimeline> DrmTimeline::Create(
    int drm_fd,
    wp_linux_drm_syncobj_manager_v1* manager) {
  std::unique_ptr<DrmTimeline> timeline(new DrmTimeline(drm_fd));
  if (!timeline->Initialize(manager)) {
    return nullptr;
  }
  return timeline;
}

DrmTimeline::DrmTimeline(int drm_fd) : drm_fd_(drm_fd) {}

DrmTimeline::~DrmTimeline() {
  if (timeline_ != nullptr) {
    wp_linux_drm_syncobj_timeline_v1_destroy(timeline_);
  }
  if (binary_handle_ != 0) {
    drmSyncobjDestroy(drm_fd_, binary_handle_);
  }
  if (handle_ != 0) {
    drmSyncobjDestroy(drm_fd_, handle_);
  }
}

bool DrmTimeline::Initialize(wp_linux_drm_syncobj_manager_v1* manager) {
  if (drmSyncobjCreate(drm_fd_, 0, &handle_) != 0 ||
      drmSyncobjCreate(drm_fd_, 0, &binary_handle_) != 0) {
    FML_LOG(ERROR) << "Failed to create DRM syncobj: " << strerror(errno);
    return false;
  }

  int fd = -1;
  if (drmSyncobjHandleToFD(drm_fd_, handle_, &fd) != 0) {
    FML_LOG(ERROR) << "Failed to export DRM syncobj: " << strerror(errno);
    return false;
  }
  fml::UniqueFD timeline_fd(fd);
  timeline_ = wp_linux_drm_syncobj_manager_v1_import_timeline(
      manager, timeline_fd.get());
  return true;
}

bool DrmTimeline::ImportSyncFile(uint64_t point,
                                 const fml::UniqueFD& sync_file) {
  if (drmSyncobjImportSyncFile(drm_fd_, binary_handle_, sync_file.get()) != 0 ||
      drmSyncobjTransfer(drm_fd_, handle_, point, binary_handle_, 0, 0) != 0) {
    FML_LOG(ERROR) << "Failed to import fence: " << strerror(errno);
    return false;
  }
  return true;
}

bool DrmTimeline::Signal(uint64_t point) {
  if (drmSyncobjTimelineSignal(drm_fd_, &handle_, &point, 1) != 0) {
    FML_LOG(ERROR) << "Failed to signal DRM syncobj: " << strerror(errno);
    return false;
  }
  return true;
}

bool DrmTimeline::IsAvailable(uint64_t point) const {
  uint32_t handle = handle_;
  return drmSyncobjTimelineWait(drm_fd_, &handle, &point, 1, 0,
                                DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
                                    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE,
                                nullptr) == 0;
}

fml::UniqueFD DrmTimeline::ExportSyncFile(uint64_t point) {
  int fd = -1;
  if (drmSyncobjTransfer(drm_fd_, binary_handle_, 0, handle_, point, 0) != 0 ||
      drmSyncobjExportSyncFile(drm_fd_, binary_handle_, &fd) != 0) {
    FML_LOG(ERROR) << "Failed to export fence: " << strerror(errno);
    return fml::UniqueFD();
  }
  return fml::UniqueFD(fd);
}

bool DrmTimeline::Wait(uint64_t point, int64_t deadline_ns) const {
  uint32_t handle = handle_;
  return drmSyncobjTimelineWait(drm_fd_, &handle, &point, 1, deadline_ns,
                                DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
                                nullptr) == 0;
}

bool DrmTimeline::WaitAnyAvailable(DrmTimeline* const* timelines,
                                   const uint64_t* points,
                                   size_t count,
                                   int64_t deadline_ns) {
  if (count == 0) {
    return false;
  }
  std::vector<uint32_t> handles(count);
  std::vector<uint64_t> wait_points(points, points + count);
  for (size_t i = 0; i < count; i++) {
    handles[i] = timelines[i]->handle_;
  }
  return drmSyncobjTimelineWait(timelines[0]->drm_fd_, handles.data(),
                                wait_points.data(), count, deadline_ns,
                                DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
                                    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE,
                                nullptr) == 0;
}

}  // namespace flutter
