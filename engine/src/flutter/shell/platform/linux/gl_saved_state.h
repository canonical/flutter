// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_GL_SAVED_STATE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_GL_SAVED_STATE_H_

#include <GLES2/gl2.h>

#include "flutter/fml/macros.h"

namespace flutter {

// Saves the OpenGL state changed by drawing a quad with one vertex attribute
// and restores it when destroyed, so drawing doesn't disturb the renderer.
class GLSavedState {
 public:
  explicit GLSavedState(GLuint attribute) : attribute_(attribute) {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_);
    glGetIntegerv(GL_VIEWPORT, viewport_);
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box_);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture_);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture_);
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment_);
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

  ~GLSavedState() {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glActiveTexture(active_texture_);
    glPixelStorei(GL_PACK_ALIGNMENT, pack_alignment_);
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
  GLint active_texture_ = GL_TEXTURE0;
  GLint texture_ = 0;
  GLint pack_alignment_ = 4;
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

  FML_DISALLOW_COPY_AND_ASSIGN(GLSavedState);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_GL_SAVED_STATE_H_
