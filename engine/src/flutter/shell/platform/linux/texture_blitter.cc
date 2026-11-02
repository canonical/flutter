// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/texture_blitter.h"

#include <GLES2/gl2ext.h>

#include <algorithm>
#include <cmath>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/gl_saved_state.h"

namespace flutter {

namespace {

// Maps the bottom of the framebuffer to the top of the texture, unless the
// texture isn't flipped.
constexpr char kVertexShader[] = R"(
attribute vec2 a_position;
uniform bool u_flip;
varying vec2 v_texcoord;
void main() {
  v_texcoord =
      vec2(a_position.x + 1.0, u_flip ? 1.0 - a_position.y : 1.0 + a_position.y) *
      0.5;
  gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

constexpr char kFragmentShader[] = R"(
precision mediump float;
uniform sampler2D u_texture;
uniform bool u_swap_red_blue;
varying vec2 v_texcoord;
void main() {
  vec4 color = texture2D(u_texture, v_texcoord);
  gl_FragColor = u_swap_red_blue ? color.bgra : color;
}
)";

// Maps the unit square to |u_rect| (in normalized device coordinates), with
// the first row of the texture at the bottom of the framebuffer (its first
// row) unless flipped.
constexpr char kLayerVertexShader[] = R"(
attribute vec2 a_position;
uniform vec4 u_rect;
uniform bool u_flip;
varying vec2 v_texcoord;
void main() {
  v_texcoord = u_flip ? vec2(a_position.x, 1.0 - a_position.y) : a_position;
  gl_Position = vec4(u_rect.xy + a_position * u_rect.zw, 0.0, 1.0);
}
)";

constexpr char kLayerFragmentShader[] = R"(
precision mediump float;
uniform sampler2D u_texture;
uniform float u_opacity;
varying vec2 v_texcoord;
void main() {
  gl_FragColor = texture2D(u_texture, v_texcoord) * u_opacity;
}
)";

constexpr char kExternalLayerFragmentShader[] = R"(
#extension GL_OES_EGL_image_external : require
precision mediump float;
uniform samplerExternalOES u_texture;
uniform float u_opacity;
varying vec2 v_texcoord;
void main() {
  gl_FragColor = texture2D(u_texture, v_texcoord) * u_opacity;
}
)";

GLuint CompileShader(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled != GL_TRUE) {
    char log[1024] = {};
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    FML_LOG(ERROR) << "Failed to compile blit shader: " << log;
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint LinkProgram(const char* vertex_source, const char* fragment_source) {
  GLuint vertex_shader = CompileShader(GL_VERTEX_SHADER, vertex_source);
  GLuint fragment_shader = CompileShader(GL_FRAGMENT_SHADER, fragment_source);
  if (vertex_shader == 0 || fragment_shader == 0) {
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return 0;
  }
  GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    char log[1024] = {};
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    FML_LOG(ERROR) << "Failed to link blit shader: " << log;
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

}  // namespace

TextureBlitter::~TextureBlitter() {
  if (vertex_buffer_ != 0) {
    glDeleteBuffers(1, &vertex_buffer_);
  }
  if (quad_buffer_ != 0) {
    glDeleteBuffers(1, &quad_buffer_);
  }
  for (const LayerProgram& layer_program : layer_programs_) {
    if (layer_program.program != 0) {
      glDeleteProgram(layer_program.program);
    }
  }
  if (program_ != 0) {
    glDeleteProgram(program_);
  }
}

bool TextureBlitter::Initialize() {
  GLuint vertex_shader = CompileShader(GL_VERTEX_SHADER, kVertexShader);
  GLuint fragment_shader = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
  if (vertex_shader == 0 || fragment_shader == 0) {
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return false;
  }
  program_ = glCreateProgram();
  glAttachShader(program_, vertex_shader);
  glAttachShader(program_, fragment_shader);
  glLinkProgram(program_);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  GLint linked = GL_FALSE;
  glGetProgramiv(program_, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    char log[1024] = {};
    glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
    FML_LOG(ERROR) << "Failed to link blit shader: " << log;
    return false;
  }
  position_location_ = glGetAttribLocation(program_, "a_position");
  texture_location_ = glGetUniformLocation(program_, "u_texture");
  swap_location_ = glGetUniformLocation(program_, "u_swap_red_blue");
  flip_location_ = glGetUniformLocation(program_, "u_flip");

  // A triangle covering the viewport.
  static constexpr GLfloat kVertices[] = {-1, -1, 3, -1, -1, 3};
  GLint array_buffer = 0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
  glGenBuffers(1, &vertex_buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, array_buffer);
  return true;
}

bool TextureBlitter::Draw(GLuint texture,
                          GLuint framebuffer,
                          int width,
                          int height,
                          bool flip,
                          bool swap_red_blue) {
  if (!initialized_) {
    initialized_ = true;
    if (!Initialize()) {
      glDeleteProgram(program_);
      program_ = 0;
    }
  }
  if (program_ == 0 || position_location_ < 0) {
    return false;
  }

  GLSavedState saved(position_location_);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glViewport(0, 0, width, height);
  glUseProgram(program_);
  glUniform1i(texture_location_, 0);
  glUniform1i(swap_location_, swap_red_blue ? 1 : 0);
  glUniform1i(flip_location_, flip ? 1 : 0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glDisable(GL_BLEND);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
  glEnableVertexAttribArray(position_location_);
  glVertexAttribPointer(position_location_, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  return true;
}

bool TextureBlitter::InitializeLayerProgram(LayerProgram* layer_program,
                                            bool external) {
  layer_program->program =
      LinkProgram(kLayerVertexShader, external ? kExternalLayerFragmentShader
                                               : kLayerFragmentShader);
  if (layer_program->program == 0) {
    return false;
  }
  GLuint program = layer_program->program;
  layer_program->position_location = glGetAttribLocation(program, "a_position");
  layer_program->texture_location = glGetUniformLocation(program, "u_texture");
  layer_program->rect_location = glGetUniformLocation(program, "u_rect");
  layer_program->flip_location = glGetUniformLocation(program, "u_flip");
  layer_program->opacity_location = glGetUniformLocation(program, "u_opacity");

  if (quad_buffer_ == 0) {
    static constexpr GLfloat kVertices[] = {0, 0, 1, 0, 0, 1, 1, 1};
    GLint array_buffer = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
    glGenBuffers(1, &quad_buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_buffer_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, array_buffer);
  }
  return layer_program->position_location >= 0;
}

bool TextureBlitter::DrawLayer(GLenum target,
                               GLuint texture,
                               GLuint framebuffer,
                               int width,
                               int height,
                               const PixelRect& dest,
                               const PixelRect& clip,
                               double opacity,
                               bool flip) {
  bool external = target == GL_TEXTURE_EXTERNAL_OES;
  LayerProgram* layer_program = &layer_programs_[external ? 1 : 0];
  if (!layer_program->initialized) {
    layer_program->initialized = true;
    if (!InitializeLayerProgram(layer_program, external)) {
      glDeleteProgram(layer_program->program);
      layer_program->program = 0;
    }
  }
  if (layer_program->program == 0 || width <= 0 || height <= 0) {
    return false;
  }

  // Only whole pixels can be clipped to.
  PixelRect bounds = {0, 0, static_cast<double>(width),
                      static_cast<double>(height)};
  PixelRect visible = clip.Intersect(dest).Intersect(bounds);
  if (visible.empty() || opacity <= 0) {
    return true;
  }
  GLint clip_x = static_cast<GLint>(std::round(visible.x));
  GLint clip_y = static_cast<GLint>(std::round(visible.y));
  GLsizei clip_width =
      static_cast<GLsizei>(std::round(visible.right())) - clip_x;
  GLsizei clip_height =
      static_cast<GLsizei>(std::round(visible.bottom())) - clip_y;
  if (clip_width <= 0 || clip_height <= 0) {
    return true;
  }

  GLSavedState saved(layer_program->position_location);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glViewport(0, 0, width, height);
  glUseProgram(layer_program->program);
  glUniform1i(layer_program->texture_location, 0);
  glUniform4f(layer_program->rect_location, dest.x / width * 2 - 1,
              dest.y / height * 2 - 1, dest.width / width * 2,
              dest.height / height * 2);
  glUniform1i(layer_program->flip_location, flip ? 1 : 0);
  glUniform1f(layer_program->opacity_location,
              static_cast<GLfloat>(std::min(opacity, 1.0)));
  glBindTexture(target, texture);
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  // Colors are premultiplied by alpha.
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glBlendEquation(GL_FUNC_ADD);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_SCISSOR_TEST);
  glScissor(clip_x, clip_y, clip_width, clip_height);
  glBindBuffer(GL_ARRAY_BUFFER, quad_buffer_);
  glEnableVertexAttribArray(layer_program->position_location);
  glVertexAttribPointer(layer_program->position_location, 2, GL_FLOAT, GL_FALSE,
                        0, nullptr);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  if (external) {
    // The saved state only includes the 2D texture binding.
    glBindTexture(target, 0);
  }
  return true;
}

}  // namespace flutter
