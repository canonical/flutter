// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_TEXTURE_BLITTER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_TEXTURE_BLITTER_H_

#include <GLES2/gl2.h>

#include "flutter/fml/macros.h"

namespace flutter {

// Copies textures into framebuffers with a shader, for display servers that
// can't show rendered buffers directly.
class TextureBlitter {
 public:
  TextureBlitter() = default;

  // Must be destroyed with the render context current, or after it has been
  // destroyed.
  ~TextureBlitter();

  // Replaces the contents of |framebuffer|, which is |width| x |height|
  // pixels, with |texture|, flipping it vertically so a texture rendered with
  // OpenGL conventions (bottom row first) has its top row first. If
  // |swap_red_blue| is true the red and blue channels are exchanged, so
  // reading the result as RGBA gives BGRA.
  //
  // The OpenGL state is restored afterwards. Must be called with the render
  // context current.
  bool Draw(GLuint texture,
            GLuint framebuffer,
            int width,
            int height,
            bool swap_red_blue);

 private:
  bool Initialize();

  bool initialized_ = false;
  GLuint program_ = 0;
  GLuint vertex_buffer_ = 0;
  GLint position_location_ = -1;
  GLint texture_location_ = -1;
  GLint swap_location_ = -1;

  FML_DISALLOW_COPY_AND_ASSIGN(TextureBlitter);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_TEXTURE_BLITTER_H_
