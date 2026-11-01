// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_DISPLAY_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_DISPLAY_H_

#include <xf86drmMode.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/fml/unique_fd.h"
#include "flutter/shell/platform/linux/display.h"

namespace flutter {

class KmsClipboard;
class KmsInput;
class KmsSeat;
class KmsSession;
class KmsWindow;

// Shows a single full-screen window directly on a screen using kernel
// modesetting, for systems without a display server.
//
// The device is the first with a connected screen, or FLUTTER_KMS_DEVICE
// (e.g. /dev/dri/card1). The screen is the first connected, or
// FLUTTER_KMS_CONNECTOR (e.g. HDMI-A-1). FLUTTER_KMS_SCALE overrides the
// scale chosen from the screen's pixel density.
//
// The screen is configured with atomic modesetting. The legacy modesetting
// calls are used if the driver doesn't support atomic modesetting, if it
// rejects the configuration, or if FLUTTER_KMS_NO_ATOMIC is set.
class KmsDisplay : public Display {
 public:
  // Returns nullptr if no screen can be used.
  static std::unique_ptr<KmsDisplay> Create(EventLoop* event_loop);

  ~KmsDisplay() override;

  // EventLoop::Source
  int GetFd() const override { return drm_fd_.get(); }
  void Dispatch(bool readable) override;

  // Display
  Backend backend() const override { return Backend::kKms; }
  Clipboard* clipboard() const override;
  std::vector<Output> GetOutputs() const override;
  std::optional<dev_t> main_device() const override { return device_; }
  void SetCursor(uint32_t shape) override;
  void RingBell(Window* window) override {}
  std::unique_ptr<Window> CreateWindow(DecorationRenderer* decoration_renderer,
                                       Window::Delegate* delegate,
                                       const Window::Params& params) override;

  EventLoop* event_loop() const { return event_loop_; }
  int drm_fd() const { return drm_fd_.get(); }
  uint32_t crtc_id() const { return crtc_id_; }
  uint32_t connector_id() const { return connector_id_; }
  const drmModeModeInfo& mode() const { return mode_; }
  double scale() const { return scale_; }
  bool supports_modifiers() const { return supports_modifiers_; }
  KmsSeat* seat() const { return seat_.get(); }

  // True if the session is in the foreground, so the screen can be used.
  bool active() const;

  // The time between screen refreshes in nanoseconds.
  uint64_t refresh_ns() const;

  // Converts a page flip time to nanoseconds on CLOCK_MONOTONIC.
  uint64_t FlipTimeNs(unsigned int sec, unsigned int usec) const;

  // True if frames can be shown with a fence the display waits for, rather
  // than rendering having to finish first.
  bool supports_fences() const { return atomic_ && in_fence_fd_prop_ != 0; }

  // Configures the screen to show the framebuffer |fb_id| once |fence_fd|
  // signals, if it is not -1. Blocks until the frame is shown.
  bool SetMode(uint32_t fb_id, int fence_fd);

  // Shows the framebuffer |fb_id| at the next vertical blank once
  // |fence_fd| signals, if it is not -1. The window's OnPageFlip is called
  // when it is shown.
  bool Flip(uint32_t fb_id, int fence_fd);

  // Called by the window when it is destroyed.
  void RemoveWindow(KmsWindow* window);

  // Shows the cursor again after the screen is configured.
  void RefreshCursor();

 private:
  explicit KmsDisplay(EventLoop* event_loop);

  bool Initialize();
  // Opens |path| and picks a connected screen on it. Returns false if it
  // has none.
  bool OpenDevice(const std::string& path, const char* connector_name);
  void ChooseScale(const drmModeConnector* connector);
  // Enables atomic modesetting. Returns false if it can't be used.
  bool InitializeAtomic();
  // Shows |fb_id| with an atomic commit. With |modeset| the screen is also
  // configured, blocking until it is done.
  bool CommitAtomic(uint32_t fb_id, int fence_fd, bool modeset);
  // Stops using atomic modesetting after it failed.
  void DisableAtomic();
  void OnActiveChanged(bool active);
  void CreateCursor();
  void UpdateCursor();
  void RestoreCrtc();

  EventLoop* event_loop_;
  std::unique_ptr<KmsSession> session_;
  std::unique_ptr<KmsSeat> seat_;
  std::unique_ptr<KmsInput> input_;
  std::unique_ptr<KmsClipboard> clipboard_;
  fml::UniqueFD drm_fd_;
  std::optional<dev_t> device_;
  uint32_t crtc_id_ = 0;
  uint32_t connector_id_ = 0;
  std::string connector_name_;
  drmModeModeInfo mode_ = {};
  drmModeCrtc* saved_crtc_ = nullptr;
  double scale_ = 1.0;
  bool monotonic_timestamps_ = false;
  bool supports_modifiers_ = false;
  KmsWindow* window_ = nullptr;

  // Atomic modesetting state. Property IDs are 0 if the driver doesn't have
  // them. |atomic_| may be cleared on the thread showing a frame.
  std::atomic<bool> atomic_ = false;
  uint32_t plane_id_ = 0;
  uint32_t mode_blob_id_ = 0;
  uint32_t connector_crtc_id_prop_ = 0;
  uint32_t crtc_active_prop_ = 0;
  uint32_t crtc_mode_id_prop_ = 0;
  uint32_t fb_id_prop_ = 0;
  uint32_t crtc_id_prop_ = 0;
  uint32_t src_x_prop_ = 0;
  uint32_t src_y_prop_ = 0;
  uint32_t src_w_prop_ = 0;
  uint32_t src_h_prop_ = 0;
  uint32_t crtc_x_prop_ = 0;
  uint32_t crtc_y_prop_ = 0;
  uint32_t crtc_w_prop_ = 0;
  uint32_t crtc_h_prop_ = 0;
  uint32_t in_fence_fd_prop_ = 0;
  // Other planes that can be shown on the CRTC, which are turned off when
  // the screen is configured. Each is the plane and its FB_ID and CRTC_ID
  // property IDs.
  struct OtherPlane {
    uint32_t id;
    uint32_t fb_id_prop;
    uint32_t crtc_id_prop;
  };
  std::vector<OtherPlane> other_planes_;

  // The cursor is drawn by the display hardware from a dumb buffer.
  uint32_t cursor_handle_ = 0;
  uint32_t cursor_width_ = 0;
  uint32_t cursor_height_ = 0;
  bool cursor_hidden_ = false;
  bool cursor_shown_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(KmsDisplay);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_DISPLAY_H_
