// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_TEXTURE_BLITTER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_TEXTURE_BLITTER_H_

#include <GLES2/gl2.h>

#include <array>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/platform_view_geometry.h"

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
  // pixels, with |texture|. If |flip| is true it is flipped vertically, so a
  // texture rendered with OpenGL conventions (bottom row first) has its top
  // row first. If |swap_red_blue| is true the red and blue channels are
  // exchanged, so reading the result as RGBA gives BGRA.
  //
  // The OpenGL state is restored afterwards. Must be called with the render
  // context current.
  bool Draw(GLuint texture,
            GLuint framebuffer,
            int width,
            int height,
            bool flip,
            bool swap_red_blue);

  // Blends |texture| over the contents of |framebuffer|, which is |width| x
  // |height| pixels with its first row at the top of the screen. The texture
  // is stretched to |dest| and only drawn inside |clip|, both in pixels from
  // the top left, and has its (premultiplied) colors multiplied by
  // |opacity|. |target| is GL_TEXTURE_2D or GL_TEXTURE_EXTERNAL_OES. If
  // |flip| is true the texture's first row is drawn at the bottom, as for
  // textures rendered with OpenGL conventions.
  //
  // The OpenGL state is restored afterwards. Must be called with the render
  // context current.
  bool DrawLayer(GLenum target,
                 GLuint texture,
                 GLuint framebuffer,
                 int width,
                 int height,
                 const PixelRect& dest,
                 const PixelRect& clip,
                 double opacity,
                 bool flip);

 private:
  // A program for drawing layers from a texture target.
  struct LayerProgram {
    bool initialized = false;
    GLuint program = 0;
    GLint position_location = -1;
    GLint texture_location = -1;
    GLint rect_location = -1;
    GLint flip_location = -1;
    GLint opacity_location = -1;
  };

  bool Initialize();
  bool InitializeLayerProgram(LayerProgram* program, bool external);

  bool initialized_ = false;
  GLuint program_ = 0;
  GLuint vertex_buffer_ = 0;
  GLint position_location_ = -1;
  GLint texture_location_ = -1;
  GLint swap_location_ = -1;
  GLint flip_location_ = -1;

  // Programs for GL_TEXTURE_2D and GL_TEXTURE_EXTERNAL_OES layers.
  std::array<LayerProgram, 2> layer_programs_;
  GLuint quad_buffer_ = 0;

  FML_DISALLOW_COPY_AND_ASSIGN(TextureBlitter);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_TEXTURE_BLITTER_H_
