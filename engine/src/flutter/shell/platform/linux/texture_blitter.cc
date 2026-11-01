// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/texture_blitter.h"

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/gl_saved_state.h"

namespace flutter {

namespace {

// Maps the bottom of the framebuffer to the top of the texture.
constexpr char kVertexShader[] = R"(
attribute vec2 a_position;
varying vec2 v_texcoord;
void main() {
  v_texcoord = vec2(a_position.x + 1.0, 1.0 - a_position.y) * 0.5;
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

}  // namespace

TextureBlitter::~TextureBlitter() {
  if (vertex_buffer_ != 0) {
    glDeleteBuffers(1, &vertex_buffer_);
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

}  // namespace flutter
