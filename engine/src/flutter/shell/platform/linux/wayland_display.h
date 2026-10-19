// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_DISPLAY_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_WAYLAND_DISPLAY_H_

#include <sys/types.h>
#include <time.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/event_loop.h"
#include "flutter/shell/platform/linux/protocols/fifo-v1-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/fractional-scale-v1-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/linux-dmabuf-v1-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/linux-drm-syncobj-v1-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/presentation-time-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/viewporter-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/wayland-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/xdg-activation-v1-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/xdg-decoration-unstable-v1-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/xdg-dialog-v1-client-protocol.h"
#include "flutter/shell/platform/linux/protocols/xdg-shell-client-protocol.h"
#include "flutter/shell/platform/linux/wayland_seat.h"

namespace flutter {

// A connection to a Wayland compositor and the globals the embedder uses.
//
// Events for objects on the default queue are dispatched on the event loop
// thread when this is added as a source to the loop.
class WaylandDisplay : public EventLoop::Source {
 public:
  // Connects to the compositor specified by the environment. Returns nullptr
  // if unable to connect or the compositor is missing required features.
  //
  // |event_loop| is the loop the display is dispatched from, and must
  // outlive it.
  static std::unique_ptr<WaylandDisplay> Create(EventLoop* event_loop);

  ~WaylandDisplay() override;

  // A Wayland global in use.
  struct Protocol {
    // The interface name, e.g. "wp_presentation".
    const char* name;

    // The version bound, which is the lower of the version the compositor
    // supports and the version the embedder supports.
    uint32_t version;
  };

  wl_display* display() const { return display_; }
  wl_compositor* compositor() const { return compositor_; }
  xdg_wm_base* wm_base() const { return wm_base_; }
  wl_shm* shm() const { return shm_; }

  // Optional globals, may be nullptr.
  wl_subcompositor* subcompositor() const { return subcompositor_; }
  zwp_linux_dmabuf_v1* dmabuf() const { return dmabuf_; }
  wp_viewporter* viewporter() const { return viewporter_; }
  wp_fractional_scale_manager_v1* fractional_scale_manager() const {
    return fractional_scale_manager_;
  }
  wp_presentation* presentation() const { return presentation_; }
  wp_fifo_manager_v1* fifo_manager() const { return fifo_manager_; }
  wp_linux_drm_syncobj_manager_v1* syncobj_manager() const {
    return syncobj_manager_;
  }
  xdg_wm_dialog_v1* wm_dialog() const { return wm_dialog_; }
  xdg_activation_v1* activation() const { return activation_; }
  zxdg_decoration_manager_v1* decoration_manager() const {
    return decoration_manager_;
  }

  // The first seat the compositor advertised, or nullptr if it has none.
  WaylandSeat* seat() const { return seat_.get(); }

  // Converts a timestamp from wp_presentation to nanoseconds on
  // CLOCK_MONOTONIC, the clock the engine and event loop use.
  uint64_t PresentationTimeToMonotonic(uint64_t seconds,
                                       uint32_t nanoseconds) const;

  // The globals bound from the compositor, in the order they were bound.
  const std::vector<Protocol>& protocols() const { return protocols_; }

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
  WaylandDisplay(wl_display* display, EventLoop* event_loop);

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
  static void PresentationClockId(void* data,
                                  wp_presentation* presentation,
                                  uint32_t clock_id);
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
  static const wp_presentation_listener kPresentationListener;
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
  wl_shm* shm_ = nullptr;
  wl_subcompositor* subcompositor_ = nullptr;
  EventLoop* event_loop_;
  wl_seat* wl_seat_ = nullptr;
  wp_cursor_shape_manager_v1* cursor_shape_manager_ = nullptr;
  zwp_text_input_manager_v3* text_input_manager_ = nullptr;
  std::unique_ptr<WaylandSeat> seat_;
  zwp_linux_dmabuf_v1* dmabuf_ = nullptr;
  wp_viewporter* viewporter_ = nullptr;
  wp_fractional_scale_manager_v1* fractional_scale_manager_ = nullptr;
  wp_presentation* presentation_ = nullptr;
  std::vector<Protocol> protocols_;
  wp_fifo_manager_v1* fifo_manager_ = nullptr;
  wp_linux_drm_syncobj_manager_v1* syncobj_manager_ = nullptr;
  xdg_wm_dialog_v1* wm_dialog_ = nullptr;
  xdg_activation_v1* activation_ = nullptr;
  zxdg_decoration_manager_v1* decoration_manager_ = nullptr;

  // The clock presentation timestamps are in.
  clockid_t presentation_clock_ = CLOCK_MONOTONIC;

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
