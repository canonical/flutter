// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_CORNER_MASK_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_CORNER_MASK_H_

#include <GLES2/gl2.h>

#include "flutter/fml/macros.h"

namespace flutter {

// Rounds the bottom corners of Flutter's content, so it matches the shape of
// a decorated window.
//
// The pixels outside the corners are made transparent in place, so the
// buffer can still be handed directly to the compositor. Only the corners are
// drawn to, and the OpenGL state is restored afterwards so the engine's
// renderer is unaffected.
//
// Used on the raster thread with the render context current.
class CornerMask {
 public:
  CornerMask() = default;
  ~CornerMask();

  // Masks the bottom corners of |framebuffer|, which is |width| x |height|
  // pixels with the bottom row first, using |radius| in pixels.
  bool Apply(GLuint framebuffer, int width, int height, float radius);

 private:
  bool Initialize();

  bool initialized_ = false;
  GLuint program_ = 0;
  GLuint vertex_buffer_ = 0;
  GLint position_location_ = -1;
  GLint size_location_ = -1;
  GLint radius_location_ = -1;

  FML_DISALLOW_COPY_AND_ASSIGN(CornerMask);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_CORNER_MASK_H_
