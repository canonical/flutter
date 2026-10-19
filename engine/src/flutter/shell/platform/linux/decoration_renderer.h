// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DECORATION_RENDERER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DECORATION_RENDERER_H_

#include <array>
#include <memory>
#include <vector>

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/linux/egl_manager.h"
#include "flutter/shell/platform/linux/render_buffer.h"
#include "flutter/shell/platform/linux/title_renderer.h"
#include "flutter/shell/platform/linux/wayland_display.h"

namespace flutter {

// What to draw in a window decoration buffer. Positions and sizes are in
// buffer pixels with the origin at the top left. Colors are non-premultiplied
// RGBA.
struct DecorationScene {
  using Color = std::array<float, 4>;

  // A circle, used to highlight a button.
  struct Circle {
    float x;
    float y;
    float radius;
    Color color;
  };

  // A line with round ends, used to draw icons.
  struct Line {
    float x0;
    float y0;
    float x1;
    float y1;
  };

  // The outline of the window, including the title bar.
  float window_x = 0;
  float window_y = 0;
  float window_width = 0;
  float window_height = 0;

  // The radius of the corners of the window.
  float corner_radius = 0;

  // True to draw a drop shadow around the window.
  bool shadow = false;

  // The size of logical pixels, used to scale the shadow.
  float scale = 1;

  float title_bar_height = 0;
  Color title_bar_color = {0, 0, 0, 0};

  // The title text, drawn with its top left corner at |title_x|, |title_y|,
  // which should be whole pixels.
  std::shared_ptr<const AlphaImage> title;
  float title_x = 0;
  float title_y = 0;
  Color title_color = {0, 0, 0, 0};

  std::vector<Circle> circles;

  std::vector<Line> lines;
  float line_width = 1;
  Color line_color = {0, 0, 0, 0};
};

// Draws window decorations on the GPU.
//
// Decorations are drawn on the platform thread with their own GL context, so
// they don't interact with Flutter's rendering on the raster thread. Buffer
// events are dispatched on the default queue.
class DecorationRenderer {
 public:
  // The maximum number of lines in a scene.
  static constexpr size_t kMaxLines = 16;

  // The maximum number of circles in a scene.
  static constexpr size_t kMaxCircles = 3;

  // Returns nullptr if decorations can't be drawn.
  static std::unique_ptr<DecorationRenderer> Create(GpuDevice* gpu,
                                                    WaylandDisplay* display);

  ~DecorationRenderer();

  // Makes the decoration context current, which is required to create,
  // draw or destroy buffers.
  bool MakeCurrent();

  // Allocates a buffer of the given size in pixels.
  std::unique_ptr<RenderBuffer> CreateBuffer(int width, int height);

  // Draws |scene| into |buffer| and waits for it to complete.
  bool Draw(RenderBuffer* buffer, const DecorationScene& scene);

  // Renders title text for scenes.
  TitleRenderer* title_renderer() { return &title_renderer_; }

 private:
  DecorationRenderer(WaylandDisplay* display, std::unique_ptr<EGLManager> egl);

  bool Initialize();

  // Uploads |image| to |title_texture_| if it isn't there already.
  void UploadTitle(const std::shared_ptr<const AlphaImage>& image);

  WaylandDisplay* display_;
  std::unique_ptr<EGLManager> egl_;
  bool use_dmabuf_ = false;
  TitleRenderer title_renderer_;

  GLuint title_texture_ = 0;
  // The image in |title_texture_|, weak so it can be compared with new
  // titles.
  std::weak_ptr<const AlphaImage> uploaded_title_;

  GLuint program_ = 0;
  GLuint vertex_buffer_ = 0;
  GLint position_location_ = -1;
  GLint size_location_ = -1;
  GLint window_location_ = -1;
  GLint corner_radius_location_ = -1;
  GLint shadow_location_ = -1;
  GLint scale_location_ = -1;
  GLint title_bar_height_location_ = -1;
  GLint title_bar_color_location_ = -1;
  GLint title_location_ = -1;
  GLint title_color_location_ = -1;
  GLint title_texture_location_ = -1;
  GLint circles_location_ = -1;
  GLint circle_colors_location_ = -1;
  GLint circle_count_location_ = -1;
  GLint lines_location_ = -1;
  GLint line_count_location_ = -1;
  GLint line_width_location_ = -1;
  GLint line_color_location_ = -1;

  FML_DISALLOW_COPY_AND_ASSIGN(DecorationRenderer);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DECORATION_RENDERER_H_
