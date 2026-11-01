// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_WINDOW_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/fml/unique_fd.h"
#include "flutter/shell/platform/linux/window.h"

namespace flutter {

class GpuDevice;
class KmsDisplay;
class KmsPlatformView;
class TextureBlitter;
class TextureBuffer;

// The window shown on the screen by a KmsDisplay. It always fills the
// screen and has no decorations.
//
// Frames are copied into buffers the display hardware scans out directly, and
// shown with a page flip at the next vertical blank. Where possible the
// display waits for a fence, so the copy doesn't have to finish first.
// Platform views and the Flutter content above them are drawn over the
// frame as it is copied.
class KmsWindow : public Window {
 public:
  KmsWindow(KmsDisplay* display, Delegate* delegate, const Params& params);
  ~KmsWindow() override;

  // Called by the display when a page flip completes, with the time it was
  // shown in nanoseconds on CLOCK_MONOTONIC.
  void OnPageFlip(uint64_t time_ns);

  // Called by the display when the session moves to the background or
  // foreground.
  void Suspend();
  void Resume();

  // Window
  int width() const override;
  int height() const override;
  double scale() const override;
  uint32_t display_id() const override;
  int physical_width() const override;
  int physical_height() const override;
  Role role() const override { return Role::kToplevel; }
  Window* parent() const override { return nullptr; }
  bool maximized() const override { return false; }
  bool fullscreen() const override { return true; }
  bool activated() const override;
  bool suspended() const override;
  bool modal() const override { return false; }
  bool resizable() const override { return false; }
  const std::string& title() const override { return title_; }
  int popup_x() const override { return 0; }
  int popup_y() const override { return 0; }
  int max_suggested_width() const override { return width(); }
  int max_suggested_height() const override { return height(); }
  bool has_pointer_focus() const override;
  bool has_keyboard_focus() const override;
  void SetInputHandlers(InputHandler* input_handler,
                        KeyboardHandler* keyboard_handler,
                        InputMethodHandler* input_method_handler) override;
  void SetTitle(const std::string& title) override { title_ = title; }
  void SetApplicationId(const std::string& application_id) override {}
  void SetSize(int width, int height) override {}
  void ResizeToContent(int width, int height) override {}
  void SetSizeLimits(const SizeLimits& limits) override {}
  void SetMaximized(bool maximized) override {}
  void SetMinimized() override {}
  void SetFullscreen(bool fullscreen) override {}
  void Activate() override {}
  void SetParent(Window* parent) override {}
  void SetPositioner(const Positioner& positioner) override {}
  void StopWaiting() override {}
  void Present(RenderBuffer* buffer,
               const SyncPoints* sync,
               const std::vector<Layer>& layers) override;
  double ContentCornerRadius(int width, int height) override { return 0; }
  size_t frames_in_flight() override;

 private:
  struct Buffer;

  // A platform view buffer drawn into a frame.
  struct ViewUse {
    std::shared_ptr<KmsPlatformView> view;
    uint64_t serial;
  };

  // Draws |layers| over |target|, where |source| is the frame they belong
  // to. Returns the platform view buffers drawn.
  std::vector<ViewUse> DrawLayers(Buffer* target,
                                  TextureBuffer* source,
                                  const std::vector<Layer>& layers);

  // Releases platform view buffers drawn into frames that have finished
  // rendering.
  void EndViewUses(std::vector<ViewUse> uses);

  // Returns a buffer that isn't shown or waiting to be shown, creating it if
  // needed. Called with the render context current.
  Buffer* AcquireBuffer(TextureBuffer* source);
  std::unique_ptr<Buffer> CreateBuffer(TextureBuffer* source);

  // Returns a fence that signals when the rendering so far is complete, or
  // an invalid fd if the display can't wait for one.
  fml::UniqueFD CreateFence(GpuDevice* gpu);

  // Shows the queued buffer. Called with |mutex_| held.
  void Submit();

  // Tells the delegate a frame was shown, from the platform thread.
  void PostFrameDone(bool presented, uint64_t time_ns);

  KmsDisplay* display_;
  Delegate* delegate_;
  std::string title_;

  std::mutex mutex_;
  std::vector<std::unique_ptr<Buffer>> buffers_;
  // The buffer on the screen.
  Buffer* front_ = nullptr;
  // The buffer that will be on the screen after the requested page flip.
  Buffer* pending_ = nullptr;
  // The buffer to show once the pending page flip completes.
  Buffer* queued_ = nullptr;
  bool active_ = true;
  // True if the screen must be configured before buffers can be flipped.
  bool needs_modeset_ = true;
  std::atomic<bool> reported_error_ = false;

  std::atomic<uint64_t> frames_presented_ = 0;
  std::atomic<uint64_t> frames_completed_ = 0;

  // Used on the raster thread.
  std::unique_ptr<TextureBlitter> blitter_;

  // Reset on destruction, so tasks can detect it.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  FML_DISALLOW_COPY_AND_ASSIGN(KmsWindow);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_WINDOW_H_
