// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_DISPLAY_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_DISPLAY_H_

#include <sys/types.h>
#include <wayland-client.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/protocols/fractional-scale-v1-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/linux-dmabuf-v1-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/viewporter-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/xdg-shell-client-protocol.h"

namespace flutter {

// A connection to a Wayland compositor and the globals the embedder uses.
//
// Events for objects on the default queue are dispatched on the event loop
// thread when this is added as a source to the loop.
class WaylandDisplay : public EventLoop::Source {
 public:
  // Connects to the compositor specified by the environment. Returns nullptr
  // if unable to connect or the compositor is missing required features.
  static std::unique_ptr<WaylandDisplay> Create();

  ~WaylandDisplay() override;

  wl_display* display() const { return display_; }
  wl_compositor* compositor() const { return compositor_; }
  xdg_wm_base* wm_base() const { return wm_base_; }
  zwp_linux_dmabuf_v1* dmabuf() const { return dmabuf_; }

  // Optional globals, may be nullptr.
  wp_viewporter* viewporter() const { return viewporter_; }
  wp_fractional_scale_manager_v1* fractional_scale_manager() const {
    return fractional_scale_manager_;
  }

  // The device the compositor prefers buffers to be allocated on, if it
  // reported one.
  std::optional<dev_t> main_device() const { return main_device_; }

  // Returns the modifiers the compositor supports for the given DRM
  // |format|. Empty if the format is not supported.
  std::vector<uint64_t> GetModifiers(uint32_t format) const;

  // Sets a callback that is called if the connection to the compositor fails.
  void SetErrorCallback(std::function<void()> callback) {
    error_callback_ = std::move(callback);
  }

  // |EventLoop::Source|
  int GetFd() const override;

  // |EventLoop::Source|
  void Prepare() override;

  // |EventLoop::Source|
  void Dispatch(bool readable) override;

 private:
  explicit WaylandDisplay(wl_display* display);

  // Binds globals and queries buffer formats. Returns false if a required
  // feature is missing.
  bool Initialize();

  // Handles a failure of the connection.
  void HandleError();

  static void RegistryGlobal(void* data,
                             wl_registry* registry,
                             uint32_t name,
                             const char* interface,
                             uint32_t version);
  static void RegistryGlobalRemove(void* data,
                                   wl_registry* registry,
                                   uint32_t name);
  static void WmBasePing(void* data, xdg_wm_base* wm_base, uint32_t serial);
  static void DmabufFormat(void* data,
                           zwp_linux_dmabuf_v1* dmabuf,
                           uint32_t format);
  static void DmabufModifier(void* data,
                             zwp_linux_dmabuf_v1* dmabuf,
                             uint32_t format,
                             uint32_t modifier_hi,
                             uint32_t modifier_lo);
  static void FeedbackDone(void* data, zwp_linux_dmabuf_feedback_v1* feedback);
  static void FeedbackFormatTable(void* data,
                                  zwp_linux_dmabuf_feedback_v1* feedback,
                                  int32_t fd,
                                  uint32_t size);
  static void FeedbackMainDevice(void* data,
                                 zwp_linux_dmabuf_feedback_v1* feedback,
                                 wl_array* device);
  static void FeedbackTrancheDone(void* data,
                                  zwp_linux_dmabuf_feedback_v1* feedback);
  static void FeedbackTrancheTargetDevice(
      void* data,
      zwp_linux_dmabuf_feedback_v1* feedback,
      wl_array* device);
  static void FeedbackTrancheFormats(void* data,
                                     zwp_linux_dmabuf_feedback_v1* feedback,
                                     wl_array* indices);
  static void FeedbackTrancheFlags(void* data,
                                   zwp_linux_dmabuf_feedback_v1* feedback,
                                   uint32_t flags);

  static const wl_registry_listener kRegistryListener;
  static const xdg_wm_base_listener kWmBaseListener;
  static const zwp_linux_dmabuf_v1_listener kDmabufListener;
  static const zwp_linux_dmabuf_feedback_v1_listener kFeedbackListener;

  // An entry in the dmabuf feedback format table.
  struct FormatTableEntry {
    uint32_t format;
    uint32_t padding;
    uint64_t modifier;
  };

  wl_display* display_;
  wl_registry* registry_ = nullptr;
  wl_compositor* compositor_ = nullptr;
  xdg_wm_base* wm_base_ = nullptr;
  zwp_linux_dmabuf_v1* dmabuf_ = nullptr;
  wp_viewporter* viewporter_ = nullptr;
  wp_fractional_scale_manager_v1* fractional_scale_manager_ = nullptr;

  std::optional<dev_t> main_device_;
  std::vector<FormatTableEntry> format_table_;
  std::map<uint32_t, std::vector<uint64_t>> modifiers_;

  bool read_prepared_ = false;
  bool failed_ = false;
  std::function<void()> error_callback_;

  FML_DISALLOW_COPY_AND_ASSIGN(WaylandDisplay);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_DISPLAY_H_
