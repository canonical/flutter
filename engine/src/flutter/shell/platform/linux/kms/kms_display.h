// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_DISPLAY_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_DISPLAY_H_

#include <xf86drmMode.h>

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
