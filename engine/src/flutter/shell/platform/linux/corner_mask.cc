// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/corner_mask.h"

#include <algorithm>
#include <cmath>

#include "flutter/fml/logging.h"

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

// The OpenGL state changed by drawing the mask.
class SavedState {
 public:
  explicit SavedState(GLuint attribute) : attribute_(attribute) {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_);
    glGetIntegerv(GL_VIEWPORT, viewport_);
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box_);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer_);
    glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb_);
    glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb_);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha_);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha_);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &blend_equation_rgb_);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blend_equation_alpha_);
    glGetBooleanv(GL_COLOR_WRITEMASK, color_mask_);
    blend_ = glIsEnabled(GL_BLEND);
    scissor_test_ = glIsEnabled(GL_SCISSOR_TEST);
    depth_test_ = glIsEnabled(GL_DEPTH_TEST);
    stencil_test_ = glIsEnabled(GL_STENCIL_TEST);
    cull_face_ = glIsEnabled(GL_CULL_FACE);

    // The attribute's state may belong to a vertex array object the
    // renderer has bound.
    glGetVertexAttribiv(attribute_, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                        &attribute_enabled_);
    glGetVertexAttribiv(attribute_, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,
                        &attribute_buffer_);
    glGetVertexAttribiv(attribute_, GL_VERTEX_ATTRIB_ARRAY_SIZE,
                        &attribute_size_);
    glGetVertexAttribiv(attribute_, GL_VERTEX_ATTRIB_ARRAY_TYPE,
                        &attribute_type_);
    glGetVertexAttribiv(attribute_, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,
                        &attribute_normalized_);
    glGetVertexAttribiv(attribute_, GL_VERTEX_ATTRIB_ARRAY_STRIDE,
                        &attribute_stride_);
    glGetVertexAttribPointerv(attribute_, GL_VERTEX_ATTRIB_ARRAY_POINTER,
                              &attribute_pointer_);
  }

  ~SavedState() {
    glBindBuffer(GL_ARRAY_BUFFER, attribute_buffer_);
    glVertexAttribPointer(attribute_, attribute_size_, attribute_type_,
                          attribute_normalized_, attribute_stride_,
                          attribute_pointer_);
    if (attribute_enabled_) {
      glEnableVertexAttribArray(attribute_);
    } else {
      glDisableVertexAttribArray(attribute_);
    }
    glBindBuffer(GL_ARRAY_BUFFER, array_buffer_);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
    glScissor(scissor_box_[0], scissor_box_[1], scissor_box_[2],
              scissor_box_[3]);
    glUseProgram(program_);
    glBlendFuncSeparate(blend_src_rgb_, blend_dst_rgb_, blend_src_alpha_,
                        blend_dst_alpha_);
    glBlendEquationSeparate(blend_equation_rgb_, blend_equation_alpha_);
    glColorMask(color_mask_[0], color_mask_[1], color_mask_[2], color_mask_[3]);
    SetEnabled(GL_BLEND, blend_);
    SetEnabled(GL_SCISSOR_TEST, scissor_test_);
    SetEnabled(GL_DEPTH_TEST, depth_test_);
    SetEnabled(GL_STENCIL_TEST, stencil_test_);
    SetEnabled(GL_CULL_FACE, cull_face_);
  }

 private:
  static void SetEnabled(GLenum capability, GLboolean enabled) {
    if (enabled) {
      glEnable(capability);
    } else {
      glDisable(capability);
    }
  }

  GLuint attribute_;
  GLint framebuffer_ = 0;
  GLint viewport_[4] = {};
  GLint scissor_box_[4] = {};
  GLint program_ = 0;
  GLint array_buffer_ = 0;
  GLint blend_src_rgb_ = GL_ONE;
  GLint blend_dst_rgb_ = GL_ZERO;
  GLint blend_src_alpha_ = GL_ONE;
  GLint blend_dst_alpha_ = GL_ZERO;
  GLint blend_equation_rgb_ = GL_FUNC_ADD;
  GLint blend_equation_alpha_ = GL_FUNC_ADD;
  GLboolean color_mask_[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
  GLboolean blend_ = GL_FALSE;
  GLboolean scissor_test_ = GL_FALSE;
  GLboolean depth_test_ = GL_FALSE;
  GLboolean stencil_test_ = GL_FALSE;
  GLboolean cull_face_ = GL_FALSE;
  GLint attribute_enabled_ = GL_FALSE;
  GLint attribute_buffer_ = 0;
  GLint attribute_size_ = 4;
  GLint attribute_type_ = GL_FLOAT;
  GLint attribute_normalized_ = GL_FALSE;
  GLint attribute_stride_ = 0;
  void* attribute_pointer_ = nullptr;
};

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

  SavedState saved(position_location_);
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
