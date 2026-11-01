// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/corner_mask.h"

#include <algorithm>
#include <cmath>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/gl_saved_state.h"

namespace flutter {

namespace {

constexpr char kVertexShader[] = R"(
attribute vec2 a_position;
void main() {
  gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

// Outputs how much of each pixel is inside the window as alpha, which the
// content is multiplied by. The content is premultiplied, so all channels
// are scaled.
constexpr char kFragmentShader[] = R"(
precision highp float;

uniform vec2 u_size;
uniform float u_radius;

void main() {
  vec2 p = gl_FragCoord.xy;
  vec2 c = vec2(clamp(p.x, u_radius, u_size.x - u_radius), u_radius);
  float d = length(p - c) - u_radius;
  gl_FragColor = vec4(clamp(0.5 - d, 0.0, 1.0));
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
    FML_LOG(ERROR) << "Failed to compile corner mask shader: " << log;
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

}  // namespace

CornerMask::~CornerMask() {
  // Called with the render context current, or after it has been destroyed
  // when the names no longer need deleting.
  if (vertex_buffer_ != 0) {
    glDeleteBuffers(1, &vertex_buffer_);
  }
  if (program_ != 0) {
    glDeleteProgram(program_);
  }
}

bool CornerMask::Initialize() {
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
    FML_LOG(ERROR) << "Failed to link corner mask shader: " << log;
    return false;
  }
  position_location_ = glGetAttribLocation(program_, "a_position");
  size_location_ = glGetUniformLocation(program_, "u_size");
  radius_location_ = glGetUniformLocation(program_, "u_radius");

  // A triangle covering the viewport, clipped to each corner with scissors.
  static constexpr GLfloat kVertices[] = {-1, -1, 3, -1, -1, 3};
  GLint array_buffer = 0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
  glGenBuffers(1, &vertex_buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, array_buffer);
  return true;
}

bool CornerMask::Apply(GLuint framebuffer,
                       int width,
                       int height,
                       float radius) {
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
  radius = std::min({radius, width / 2.0f, static_cast<float>(height)});
  int size = static_cast<int>(std::ceil(radius));
  if (size <= 0) {
    return true;
  }

  GLSavedState saved(position_location_);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glViewport(0, 0, width, height);
  glUseProgram(program_);
  glUniform2f(size_location_, width, height);
  glUniform1f(radius_location_, radius);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ZERO, GL_SRC_ALPHA);
  glBlendEquation(GL_FUNC_ADD);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_SCISSOR_TEST);
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
  glEnableVertexAttribArray(position_location_);
  glVertexAttribPointer(position_location_, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

  // The bottom row is first, so the bottom corners are at the start.
  glScissor(0, 0, size, size);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glScissor(width - size, 0, size, size);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  return true;
}

}  // namespace flutter
