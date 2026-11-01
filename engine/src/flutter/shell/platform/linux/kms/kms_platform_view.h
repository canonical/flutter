// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_PLATFORM_VIEW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_PLATFORM_VIEW_H_

#include <GLES2/gl2.h>

#include <functional>
#include <memory>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/platform_view.h"
#include "flutter/shell/platform/linux/platform_view_geometry.h"

namespace flutter {

class EGLManager;

// A platform view drawn into the frames a KmsWindow shows.
//
// Without a compositor, the window draws the latest buffer into each frame
// where the scene places the view, and asks for the last frame to be drawn
// again when a new buffer is presented. A buffer is released once it has been
// replaced and the frames it was drawn into have finished rendering.
class KmsPlatformView : public LinuxPlatformView {
 public:
  // |redraw| is called on the platform thread when a new buffer needs
  // showing.
  KmsPlatformView(int64_t id,
                  EventLoop* event_loop,
                  std::function<void()> redraw);
  ~KmsPlatformView() override;

  // Draws the current buffer at |geometry|, where |scale| is the number of
  // physical pixels per logical pixel, by calling |draw| with a texture of
  // it and its target (GL_TEXTURE_2D or GL_TEXTURE_EXTERNAL_OES). Called on
  // the raster thread with the render context current.
  //
  // Returns the serial of the buffer drawn, which must be passed to EndUse
  // once the GPU has finished drawing it, or zero if nothing was drawn.
  uint64_t Draw(EGLManager* egl,
                const PlatformViewGeometry& geometry,
                double scale,
                const std::function<bool(GLenum target, GLuint texture)>& draw);

  // Ends the use of the buffer with |serial| returned by Draw. Called on the
  // platform thread.
  void EndUse(uint64_t serial);

 protected:
  // |LinuxPlatformView|
  std::unique_ptr<Buffer> CreateBackendBuffer(
      const DmabufFrame& frame) override;
  void OnPresent() override;

 private:
  class KmsBuffer;

  std::function<void()> redraw_;

  // Warnings logged once per view.
  bool warned_approximated_ = false;

  FML_DISALLOW_COPY_AND_ASSIGN(KmsPlatformView);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_KMS_KMS_PLATFORM_VIEW_H_
