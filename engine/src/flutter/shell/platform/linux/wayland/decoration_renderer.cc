// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/wayland/decoration_renderer.h"

#include <algorithm>

#include "flutter/fml/logging.h"
#include "flutter/shell/platform/linux/wayland/dmabuf_buffer.h"
#include "flutter/shell/platform/linux/wayland/shm_buffer.h"

namespace flutter {

namespace {

constexpr char kVertexShader[] = R"(
attribute vec2 a_position;
void main() {
  gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

// Everything is drawn with signed distance functions, so shapes are
// antialiased at any scale.
constexpr char kFragmentShader[] = R"(
precision highp float;

uniform vec2 u_size;
uniform vec2 u_origin;
uniform vec4 u_window;
uniform float u_corner_radius;
uniform float u_shadow;
uniform float u_scale;
uniform float u_title_bar_height;
uniform vec4 u_title_bar_color;
uniform vec4 u_title;
uniform vec4 u_title_color;
uniform sampler2D u_title_texture;
uniform vec3 u_circles[3];
uniform vec4 u_circle_colors[3];
uniform int u_circle_count;
uniform vec4 u_lines[16];
uniform int u_line_count;
uniform float u_line_width;
uniform vec4 u_line_color;

// Signed distance from |p| to a box centred on |c| with half size |h| and
// corner radius |r|. Negative inside.
float box(vec2 p, vec2 c, vec2 h, float r) {
  vec2 q = abs(p - c) - h + r;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float window_shape(vec2 p, vec2 offset) {
  vec2 h = u_window.zw * 0.5;
  vec2 c = u_window.xy + h + offset;
  return box(p, c, h, u_corner_radius);
}

float line(vec2 p, vec4 l) {
  vec2 pa = p - l.xy;
  vec2 ba = l.zw - l.xy;
  float t = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
  return length(pa - ba * t);
}

vec4 premultiply(vec4 c) {
  return vec4(c.rgb * c.a, c.a);
}

vec4 over(vec4 src, vec4 dst) {
  return src + dst * (1.0 - src.a);
}

void main() {
  // GL rows are bottom first, the surface flips them.
  vec2 p = u_origin + vec2(gl_FragCoord.x, u_size.y - gl_FragCoord.y);
  float s = u_scale;

  float d = window_shape(p, vec2(0.0));
  float coverage = clamp(0.5 - d, 0.0, 1.0);

  // A soft shadow offset down, a tighter one around the window and a faint
  // outline.
  vec4 color = vec4(0.0);
  if (u_shadow > 0.5) {
    float key = clamp(1.0 - max(window_shape(p, vec2(0.0, 4.0 * s)), 0.0) /
                                (22.0 * s), 0.0, 1.0);
    float ambient = clamp(1.0 - max(window_shape(p, vec2(0.0, 1.0 * s)), 0.0) /
                                    (7.0 * s), 0.0, 1.0);
    float outline = 0.18 *
        clamp(1.0 - abs(d - 0.3 * s) / (0.9 * s), 0.0, 1.0) *
        step(-0.5, d);
    float a = clamp(0.28 * key * key * key +
                    0.14 * ambient * ambient * (3.0 - 2.0 * ambient) +
                    outline, 0.0, 1.0);
    color = vec4(0.0, 0.0, 0.0, a) * (1.0 - coverage);
  }

  // The rest of the window is covered by Flutter's content.
  if (p.y >= u_window.y + u_title_bar_height) {
    gl_FragColor = color;
    return;
  }

  vec4 bar = premultiply(u_title_bar_color);

  vec2 title_uv = (p - u_title.xy) / max(u_title.zw, vec2(1.0));
  if (u_title.z > 0.0 && all(greaterThanEqual(title_uv, vec2(0.0))) &&
      all(lessThan(title_uv, vec2(1.0)))) {
    float t = texture2D(u_title_texture, title_uv).a;
    bar = over(premultiply(u_title_color) * t, bar);
  }

  for (int i = 0; i < 3; i++) {
    if (i >= u_circle_count) {
      break;
    }
    float c = length(p - u_circles[i].xy) - u_circles[i].z;
    bar = over(premultiply(u_circle_colors[i]) * clamp(0.5 - c, 0.0, 1.0),
               bar);
  }

  float l = 1e6;
  for (int i = 0; i < 16; i++) {
    if (i >= u_line_count) {
      break;
    }
    l = min(l, line(p, u_lines[i]));
  }
  float line_coverage = clamp(0.5 - (l - u_line_width * 0.5), 0.0, 1.0);
  bar = over(premultiply(u_line_color) * line_coverage, bar);

  gl_FragColor = over(bar * coverage, color);
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
    FML_LOG(ERROR) << "Failed to compile decoration shader: " << log;
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

}  // namespace

std::unique_ptr<DecorationRenderer> DecorationRenderer::Create(
    GpuDevice* gpu,
    WaylandDisplay* display) {
  std::unique_ptr<EGLManager> egl = EGLManager::Create(gpu);
  if (!egl) {
    return nullptr;
  }
  std::unique_ptr<DecorationRenderer> renderer(
      new DecorationRenderer(display, std::move(egl)));
  if (!renderer->Initialize()) {
    return nullptr;
  }
  return renderer;
}

DecorationRenderer::DecorationRenderer(WaylandDisplay* display,
                                       std::unique_ptr<EGLManager> egl)
    : display_(display), egl_(std::move(egl)) {}

DecorationRenderer::~DecorationRenderer() {
  if (MakeCurrent()) {
    if (vertex_buffer_ != 0) {
      glDeleteBuffers(1, &vertex_buffer_);
    }
    if (program_ != 0) {
      glDeleteProgram(program_);
    }
    if (title_texture_ != 0) {
      glDeleteTextures(1, &title_texture_);
    }
    egl_->ClearCurrent();
  }
}

bool DecorationRenderer::Initialize() {
  use_dmabuf_ = display_->dmabuf() != nullptr &&
                egl_->gpu()->supports_dmabuf_import() &&
                !display_->GetModifiers(DmabufBuffer::kFormat).empty();

  if (!MakeCurrent()) {
    return false;
  }

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
    FML_LOG(ERROR) << "Failed to link decoration shader: " << log;
    return false;
  }

  position_location_ = glGetAttribLocation(program_, "a_position");
  size_location_ = glGetUniformLocation(program_, "u_size");
  origin_location_ = glGetUniformLocation(program_, "u_origin");
  window_location_ = glGetUniformLocation(program_, "u_window");
  corner_radius_location_ = glGetUniformLocation(program_, "u_corner_radius");
  shadow_location_ = glGetUniformLocation(program_, "u_shadow");
  scale_location_ = glGetUniformLocation(program_, "u_scale");
  title_bar_height_location_ =
      glGetUniformLocation(program_, "u_title_bar_height");
  title_bar_color_location_ =
      glGetUniformLocation(program_, "u_title_bar_color");
  title_location_ = glGetUniformLocation(program_, "u_title");
  title_color_location_ = glGetUniformLocation(program_, "u_title_color");
  title_texture_location_ = glGetUniformLocation(program_, "u_title_texture");
  circles_location_ = glGetUniformLocation(program_, "u_circles");
  circle_colors_location_ = glGetUniformLocation(program_, "u_circle_colors");
  circle_count_location_ = glGetUniformLocation(program_, "u_circle_count");
  lines_location_ = glGetUniformLocation(program_, "u_lines");
  line_count_location_ = glGetUniformLocation(program_, "u_line_count");
  line_width_location_ = glGetUniformLocation(program_, "u_line_width");
  line_color_location_ = glGetUniformLocation(program_, "u_line_color");

  // A triangle that covers the viewport.
  const GLfloat vertices[] = {-1, -1, 3, -1, -1, 3};
  glGenBuffers(1, &vertex_buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glGenTextures(1, &title_texture_);
  glBindTexture(GL_TEXTURE_2D, title_texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  return true;
}

void DecorationRenderer::UploadTitle(
    const std::shared_ptr<const AlphaImage>& image) {
  if (!image || uploaded_title_.lock() == image) {
    return;
  }
  glBindTexture(GL_TEXTURE_2D, title_texture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, image->width, image->height, 0,
               GL_ALPHA, GL_UNSIGNED_BYTE, image->pixels.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  uploaded_title_ = image;
}

bool DecorationRenderer::MakeCurrent() {
  return egl_->IsRenderContextCurrent() || egl_->MakeRenderContextCurrent();
}

std::unique_ptr<RenderBuffer> DecorationRenderer::CreateBuffer(int width,
                                                               int height) {
  if (!MakeCurrent()) {
    return nullptr;
  }
  if (use_dmabuf_) {
    std::unique_ptr<RenderBuffer> buffer = DmabufBuffer::Create(
        egl_.get(), display_->dmabuf(),
        display_->GetModifiers(DmabufBuffer::kFormat), width, height, false);
    if (buffer) {
      return buffer;
    }
    use_dmabuf_ = false;
  }
  return ShmBuffer::Create(egl_.get(), display_->shm(), width, height, false);
}

bool DecorationRenderer::Draw(const DecorationScene& scene,
                              const std::vector<Target>& targets) {
  if (!MakeCurrent()) {
    return false;
  }

  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glUseProgram(program_);

  glUniform4f(window_location_, scene.window_x, scene.window_y,
              scene.window_width, scene.window_height);
  glUniform1f(corner_radius_location_, scene.corner_radius);
  glUniform1f(shadow_location_, scene.shadow ? 1.0f : 0.0f);
  glUniform1f(scale_location_, scene.scale);
  glUniform1f(title_bar_height_location_, scene.title_bar_height);
  glUniform4fv(title_bar_color_location_, 1, scene.title_bar_color.data());
  UploadTitle(scene.title);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, title_texture_);
  glUniform1i(title_texture_location_, 0);
  if (scene.title) {
    glUniform4f(title_location_, scene.title_x, scene.title_y,
                scene.title->width, scene.title->height);
  } else {
    glUniform4f(title_location_, 0, 0, 0, 0);
  }
  glUniform4fv(title_color_location_, 1, scene.title_color.data());

  GLfloat circles[kMaxCircles * 3] = {};
  GLfloat circle_colors[kMaxCircles * 4] = {};
  size_t circle_count = std::min(scene.circles.size(), kMaxCircles);
  for (size_t i = 0; i < circle_count; i++) {
    const DecorationScene::Circle& c = scene.circles[i];
    circles[i * 3] = c.x;
    circles[i * 3 + 1] = c.y;
    circles[i * 3 + 2] = c.radius;
    std::copy(c.color.begin(), c.color.end(), circle_colors + i * 4);
  }
  glUniform3fv(circles_location_, kMaxCircles, circles);
  glUniform4fv(circle_colors_location_, kMaxCircles, circle_colors);
  glUniform1i(circle_count_location_, circle_count);

  GLfloat lines[kMaxLines * 4] = {};
  size_t line_count = std::min(scene.lines.size(), kMaxLines);
  for (size_t i = 0; i < line_count; i++) {
    const DecorationScene::Line& l = scene.lines[i];
    lines[i * 4] = l.x0;
    lines[i * 4 + 1] = l.y0;
    lines[i * 4 + 2] = l.x1;
    lines[i * 4 + 3] = l.y1;
  }
  glUniform4fv(lines_location_, kMaxLines, lines);
  glUniform1i(line_count_location_, line_count);
  glUniform1f(line_width_location_, scene.line_width);
  glUniform4fv(line_color_location_, 1, scene.line_color.data());

  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
  glEnableVertexAttribArray(position_location_);
  glVertexAttribPointer(position_location_, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  for (const Target& target : targets) {
    RenderBuffer* buffer = target.buffer;
    glBindFramebuffer(GL_FRAMEBUFFER, buffer->framebuffer());
    glViewport(0, 0, buffer->width(), buffer->height());
    glUniform2f(size_location_, buffer->width(), buffer->height());
    glUniform2f(origin_location_, target.x, target.y);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glDisableVertexAttribArray(position_location_);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  for (const Target& target : targets) {
    if (!target.buffer->PrepareToPresent()) {
      return false;
    }
  }

  // Decorations are drawn rarely, so wait for them rather than synchronizing
  // with the compositor.
  glFinish();
  return true;
}

}  // namespace flutter
