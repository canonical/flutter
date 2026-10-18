// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland_display.h"

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "flutter/fml/logging.h"

namespace flutter {

namespace {

// Minimum version of wl_compositor required, for wl_surface.damage_buffer.
constexpr uint32_t kMinCompositorVersion = 4;

// Minimum version of zwp_linux_dmabuf_v1 required, for modifier support.
constexpr uint32_t kMinDmabufVersion = 3;

// The highest versions of each interface the listeners handle. These must be
// explicit, as the versions in the wl_interface structures for the core
// protocol come from the libwayland the application is run with, which can
// be newer than the headers it was built with. Binding a newer version
// would cause the compositor to send events the listeners don't handle.
constexpr uint32_t kMaxCompositorVersion = 4;
constexpr uint32_t kMaxWmBaseVersion = 7;
constexpr uint32_t kMaxShmVersion = 1;
constexpr uint32_t kMaxDmabufVersion = 5;
constexpr uint32_t kMaxViewporterVersion = 1;
constexpr uint32_t kMaxFractionalScaleManagerVersion = 1;
constexpr uint32_t kMaxPresentationVersion = 2;
constexpr uint32_t kMaxFifoManagerVersion = 1;
constexpr uint32_t kMaxSyncobjManagerVersion = 1;

// First version of zwp_linux_dmabuf_v1 that supports feedback.
constexpr uint32_t kDmabufFeedbackVersion = 4;

// Binds a global and records it in |protocols|.
template <typename T>
T* Bind(std::vector<WaylandDisplay::Protocol>* protocols,
        wl_registry* registry,
        uint32_t name,
        const wl_interface* interface,
        uint32_t server_version,
        uint32_t max_version) {
  uint32_t version = std::min(server_version, max_version);
  protocols->push_back({interface->name, version});
  return static_cast<T*>(wl_registry_bind(registry, name, interface, version));
}

uint64_t NowNanoseconds(clockid_t clock) {
  timespec time;
  clock_gettime(clock, &time);
  return static_cast<uint64_t>(time.tv_sec) * 1000000000 + time.tv_nsec;
}

}  // namespace

const wl_registry_listener WaylandDisplay::kRegistryListener = {
    .global = RegistryGlobal,
    .global_remove = RegistryGlobalRemove,
};

const xdg_wm_base_listener WaylandDisplay::kWmBaseListener = {
    .ping = WmBasePing,
};

const wp_presentation_listener WaylandDisplay::kPresentationListener = {
    .clock_id = PresentationClockId,
};

const zwp_linux_dmabuf_v1_listener WaylandDisplay::kDmabufListener = {
    .format = DmabufFormat,
    .modifier = DmabufModifier,
};

const zwp_linux_dmabuf_feedback_v1_listener WaylandDisplay::kFeedbackListener =
    {
        .done = FeedbackDone,
        .format_table = FeedbackFormatTable,
        .main_device = FeedbackMainDevice,
        .tranche_done = FeedbackTrancheDone,
        .tranche_target_device = FeedbackTrancheTargetDevice,
        .tranche_formats = FeedbackTrancheFormats,
        .tranche_flags = FeedbackTrancheFlags,
};

std::unique_ptr<WaylandDisplay> WaylandDisplay::Create() {
  wl_display* display = wl_display_connect(nullptr);
  if (display == nullptr) {
    FML_LOG(ERROR) << "Failed to connect to Wayland compositor: "
                   << strerror(errno);
    return nullptr;
  }

  std::unique_ptr<WaylandDisplay> result(new WaylandDisplay(display));
  if (!result->Initialize()) {
    return nullptr;
  }
  return result;
}

WaylandDisplay::WaylandDisplay(wl_display* display) : display_(display) {}

WaylandDisplay::~WaylandDisplay() {
  if (read_prepared_) {
    wl_display_cancel_read(display_);
  }
  if (fractional_scale_manager_ != nullptr) {
    wp_fractional_scale_manager_v1_destroy(fractional_scale_manager_);
  }
  if (syncobj_manager_ != nullptr) {
    wp_linux_drm_syncobj_manager_v1_destroy(syncobj_manager_);
  }
  if (fifo_manager_ != nullptr) {
    wp_fifo_manager_v1_destroy(fifo_manager_);
  }
  if (presentation_ != nullptr) {
    wp_presentation_destroy(presentation_);
  }
  if (viewporter_ != nullptr) {
    wp_viewporter_destroy(viewporter_);
  }
  if (dmabuf_ != nullptr) {
    zwp_linux_dmabuf_v1_destroy(dmabuf_);
  }
  if (shm_ != nullptr) {
    wl_shm_destroy(shm_);
  }
  if (wm_base_ != nullptr) {
    xdg_wm_base_destroy(wm_base_);
  }
  if (compositor_ != nullptr) {
    wl_compositor_destroy(compositor_);
  }
  if (registry_ != nullptr) {
    wl_registry_destroy(registry_);
  }
  wl_display_disconnect(display_);
}

bool WaylandDisplay::Initialize() {
  registry_ = wl_display_get_registry(display_);
  wl_registry_add_listener(registry_, &kRegistryListener, this);
  if (wl_display_roundtrip(display_) < 0) {
    FML_LOG(ERROR) << "Failed to get Wayland globals";
    return false;
  }

  if (compositor_ == nullptr) {
    FML_LOG(ERROR) << "Wayland compositor does not support wl_compositor v"
                   << kMinCompositorVersion;
    return false;
  }
  if (wm_base_ == nullptr) {
    FML_LOG(ERROR) << "Wayland compositor does not support xdg_wm_base";
    return false;
  }
  if (shm_ == nullptr) {
    FML_LOG(ERROR) << "Wayland compositor does not support wl_shm";
    return false;
  }
  if (dmabuf_ == nullptr) {
    return true;
  }

  zwp_linux_dmabuf_feedback_v1* feedback = nullptr;
  if (zwp_linux_dmabuf_v1_get_version(dmabuf_) >= kDmabufFeedbackVersion) {
    feedback = zwp_linux_dmabuf_v1_get_default_feedback(dmabuf_);
    zwp_linux_dmabuf_feedback_v1_add_listener(feedback, &kFeedbackListener,
                                              this);
  } else {
    zwp_linux_dmabuf_v1_add_listener(dmabuf_, &kDmabufListener, this);
  }
  int result = wl_display_roundtrip(display_);
  if (feedback != nullptr) {
    zwp_linux_dmabuf_feedback_v1_destroy(feedback);
  }
  format_table_.clear();
  if (result < 0) {
    FML_LOG(ERROR) << "Failed to get Wayland buffer formats";
    return false;
  }

  return true;
}

std::vector<uint64_t> WaylandDisplay::GetModifiers(uint32_t format) const {
  auto it = modifiers_.find(format);
  if (it == modifiers_.end()) {
    return {};
  }
  return it->second;
}

int WaylandDisplay::GetFd() const {
  return wl_display_get_fd(display_);
}

void WaylandDisplay::Prepare() {
  if (failed_) {
    return;
  }

  // Events can't be read until everything already queued is dispatched.
  while (wl_display_prepare_read(display_) != 0) {
    if (wl_display_dispatch_pending(display_) < 0) {
      HandleError();
      return;
    }
  }
  read_prepared_ = true;

  // Send any requests made since the last iteration. If the socket buffer is
  // full the remainder is sent on the next iteration.
  if (wl_display_flush(display_) < 0 && errno != EAGAIN) {
    HandleError();
  }
}

void WaylandDisplay::Dispatch(bool readable) {
  if (read_prepared_) {
    read_prepared_ = false;
    if (readable) {
      if (wl_display_read_events(display_) < 0) {
        HandleError();
        return;
      }
    } else {
      wl_display_cancel_read(display_);
    }
  }

  if (failed_) {
    return;
  }
  if (wl_display_dispatch_pending(display_) < 0) {
    HandleError();
  }
}

void WaylandDisplay::HandleError() {
  if (read_prepared_) {
    wl_display_cancel_read(display_);
    read_prepared_ = false;
  }
  if (failed_) {
    return;
  }
  failed_ = true;

  int error = wl_display_get_error(display_);
  FML_LOG(ERROR) << "Lost connection to Wayland compositor: "
                 << strerror(error != 0 ? error : errno);
  if (error_callback_) {
    error_callback_();
  }
}

void WaylandDisplay::RegistryGlobal(void* data,
                                    wl_registry* registry,
                                    uint32_t name,
                                    const char* interface,
                                    uint32_t version) {
  auto* self = static_cast<WaylandDisplay*>(data);
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    if (version >= kMinCompositorVersion) {
      self->compositor_ = Bind<wl_compositor>(&self->protocols_, registry, name,
                                              &wl_compositor_interface, version,
                                              kMaxCompositorVersion);
    }
  } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
    self->wm_base_ =
        Bind<xdg_wm_base>(&self->protocols_, registry, name,
                          &xdg_wm_base_interface, version, kMaxWmBaseVersion);
    xdg_wm_base_add_listener(self->wm_base_, &kWmBaseListener, self);
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    self->shm_ = Bind<wl_shm>(&self->protocols_, registry, name,
                              &wl_shm_interface, version, kMaxShmVersion);
  } else if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0) {
    if (version >= kMinDmabufVersion) {
      self->dmabuf_ = Bind<zwp_linux_dmabuf_v1>(
          &self->protocols_, registry, name, &zwp_linux_dmabuf_v1_interface,
          version, kMaxDmabufVersion);
    }
  } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
    self->viewporter_ = Bind<wp_viewporter>(&self->protocols_, registry, name,
                                            &wp_viewporter_interface, version,
                                            kMaxViewporterVersion);
  } else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) ==
             0) {
    self->fractional_scale_manager_ = Bind<wp_fractional_scale_manager_v1>(
        &self->protocols_, registry, name,
        &wp_fractional_scale_manager_v1_interface, version,
        kMaxFractionalScaleManagerVersion);
  } else if (strcmp(interface, wp_presentation_interface.name) == 0) {
    self->presentation_ = Bind<wp_presentation>(
        &self->protocols_, registry, name, &wp_presentation_interface, version,
        kMaxPresentationVersion);
    wp_presentation_add_listener(self->presentation_, &kPresentationListener,
                                 self);
  } else if (strcmp(interface, wp_fifo_manager_v1_interface.name) == 0) {
    self->fifo_manager_ = Bind<wp_fifo_manager_v1>(
        &self->protocols_, registry, name, &wp_fifo_manager_v1_interface,
        version, kMaxFifoManagerVersion);
  } else if (strcmp(interface,
                    wp_linux_drm_syncobj_manager_v1_interface.name) == 0) {
    self->syncobj_manager_ = Bind<wp_linux_drm_syncobj_manager_v1>(
        &self->protocols_, registry, name,
        &wp_linux_drm_syncobj_manager_v1_interface, version,
        kMaxSyncobjManagerVersion);
  }
}

void WaylandDisplay::PresentationClockId(void* data,
                                         wp_presentation* presentation,
                                         uint32_t clock_id) {
  static_cast<WaylandDisplay*>(data)->presentation_clock_ =
      static_cast<clockid_t>(clock_id);
}

uint64_t WaylandDisplay::PresentationTimeToMonotonic(
    uint64_t seconds,
    uint32_t nanoseconds) const {
  uint64_t time = seconds * 1000000000 + nanoseconds;
  if (presentation_clock_ == CLOCK_MONOTONIC) {
    return time;
  }
  // Convert using the current offset between the clocks.
  return time - NowNanoseconds(presentation_clock_) +
         NowNanoseconds(CLOCK_MONOTONIC);
}

void WaylandDisplay::RegistryGlobalRemove(void* data,
                                          wl_registry* registry,
                                          uint32_t name) {}

void WaylandDisplay::WmBasePing(void* data,
                                xdg_wm_base* wm_base,
                                uint32_t serial) {
  xdg_wm_base_pong(wm_base, serial);
}

void WaylandDisplay::DmabufFormat(void* data,
                                  zwp_linux_dmabuf_v1* dmabuf,
                                  uint32_t format) {
  // Deprecated, modifier events are sent for every format.
}

void WaylandDisplay::DmabufModifier(void* data,
                                    zwp_linux_dmabuf_v1* dmabuf,
                                    uint32_t format,
                                    uint32_t modifier_hi,
                                    uint32_t modifier_lo) {
  auto* self = static_cast<WaylandDisplay*>(data);
  uint64_t modifier = (static_cast<uint64_t>(modifier_hi) << 32) | modifier_lo;
  auto& modifiers = self->modifiers_[format];
  if (std::find(modifiers.begin(), modifiers.end(), modifier) ==
      modifiers.end()) {
    modifiers.push_back(modifier);
  }
}

void WaylandDisplay::FeedbackDone(void* data,
                                  zwp_linux_dmabuf_feedback_v1* feedback) {}

void WaylandDisplay::FeedbackFormatTable(void* data,
                                         zwp_linux_dmabuf_feedback_v1* feedback,
                                         int32_t fd,
                                         uint32_t size) {
  auto* self = static_cast<WaylandDisplay*>(data);
  self->format_table_.clear();
  void* table = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (table == MAP_FAILED) {
    FML_LOG(ERROR) << "Failed to map dmabuf format table: " << strerror(errno);
    return;
  }
  auto* entries = static_cast<const FormatTableEntry*>(table);
  self->format_table_.assign(entries,
                             entries + size / sizeof(FormatTableEntry));
  munmap(table, size);
}

void WaylandDisplay::FeedbackMainDevice(void* data,
                                        zwp_linux_dmabuf_feedback_v1* feedback,
                                        wl_array* device) {
  auto* self = static_cast<WaylandDisplay*>(data);
  dev_t dev;
  if (device->size != sizeof(dev)) {
    return;
  }
  memcpy(&dev, device->data, sizeof(dev));
  self->main_device_ = dev;
}

void WaylandDisplay::FeedbackTrancheDone(
    void* data,
    zwp_linux_dmabuf_feedback_v1* feedback) {}

void WaylandDisplay::FeedbackTrancheTargetDevice(
    void* data,
    zwp_linux_dmabuf_feedback_v1* feedback,
    wl_array* device) {}

void WaylandDisplay::FeedbackTrancheFormats(
    void* data,
    zwp_linux_dmabuf_feedback_v1* feedback,
    wl_array* indices) {
  auto* self = static_cast<WaylandDisplay*>(data);
  const uint16_t* index = static_cast<const uint16_t*>(indices->data);
  size_t n_indices = indices->size / sizeof(uint16_t);
  for (size_t i = 0; i < n_indices; i++) {
    if (index[i] >= self->format_table_.size()) {
      continue;
    }
    const FormatTableEntry& entry = self->format_table_[index[i]];
    DmabufModifier(self, nullptr, entry.format, entry.modifier >> 32,
                   entry.modifier & 0xffffffff);
  }
}

void WaylandDisplay::FeedbackTrancheFlags(
    void* data,
    zwp_linux_dmabuf_feedback_v1* feedback,
    uint32_t flags) {}

}  // namespace flutter
